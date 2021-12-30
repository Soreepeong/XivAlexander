#include "pch.h"

#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryProvider.h>
#include <XivAlexanderCommon/Sqex_Sqpack_EntryRawStream.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Reader.h>
#include <XivAlexanderCommon/Sqex_Sqpack_Creator.h>
#include <XivAlexanderCommon/Sqex_Texture_ModifiableTextureStream.h>
#include <XivAlexanderCommon/Utils_Win32_ThreadPool.h>

std::shared_ptr<Sqex::RandomAccessStream> StripSecondaryMipmaps(std::shared_ptr<Sqex::RandomAccessStream> src) {
	return src;

	auto stream = std::make_shared<Sqex::Texture::ModifiableTextureStream>(std::move(src));
	stream->TruncateMipmap(1);
	return stream;
}

std::shared_ptr<Sqex::RandomAccessStream> StripLodModels(std::shared_ptr<Sqex::RandomAccessStream> src) {
	return src;

	if (src->ReadStream<Sqex::Model::Header>(0).LodCount == 1)
		return src;

	auto data = src->ReadStreamIntoVector<uint8_t>(0);
	auto& header = *reinterpret_cast<Sqex::Model::Header*>(&data[0]);
	header.VertexOffset[1] = header.VertexOffset[2] = header.IndexOffset[1] = header.IndexOffset[2] = header.IndexOffset[0] + header.IndexSize[0];
	header.VertexSize[1] = header.VertexSize[2] = header.IndexSize[1] = header.IndexSize[2] = 0;
	header.LodCount = 1;
	data.resize(header.VertexOffset[1]);
	src = std::make_shared<Sqex::MemoryRandomAccessStream>(data);

	/*auto raw = std::make_shared<Sqex::Sqpack::EntryRawStream>(std::make_shared<Sqex::Sqpack::OnTheFlyModelEntryProvider>(Sqex::Sqpack::EntryPathSpec{}, src));
	auto data2 = raw->ReadStreamIntoVector<uint8_t>(0);
	std::cout << memcmp(data.data(), data2.data(), data.size());*/
	return src;
}

std::shared_ptr<Sqex::Sqpack::EntryProvider> ToDecompressedEntryProvider(std::shared_ptr<Sqex::Sqpack::EntryProvider> src) {
	auto pathSpec = src->PathSpec();
	auto rawStream = std::make_shared<Sqex::Sqpack::EntryRawStream>(std::move(src));
	switch (rawStream->EntryType()) {
		case Sqex::Sqpack::SqData::FileEntryType::Empty:
			return std::make_shared<Sqex::Sqpack::EmptyEntryProvider>(std::move(pathSpec));
		case Sqex::Sqpack::SqData::FileEntryType::Binary:
			return std::make_shared<Sqex::Sqpack::OnTheFlyBinaryEntryProvider>(std::move(pathSpec), std::move(rawStream));
		case Sqex::Sqpack::SqData::FileEntryType::Model:
			return std::make_shared<Sqex::Sqpack::OnTheFlyModelEntryProvider>(std::move(pathSpec), StripLodModels(std::move(rawStream)));
		case Sqex::Sqpack::SqData::FileEntryType::Texture:
			return std::make_shared<Sqex::Sqpack::OnTheFlyTextureEntryProvider>(std::move(pathSpec), StripSecondaryMipmaps(std::move(rawStream)));
			// return std::make_shared<Sqex::Sqpack::MemoryTextureEntryProvider>(std::move(pathSpec), StripSecondaryMipmaps(std::move(rawStream)));
	}
	throw std::runtime_error("Unknown entry type");
}

uint64_t StreamToFile(const Sqex::RandomAccessStream& stream, const Utils::Win32::Handle& file, std::vector<uint8_t>& buffer, uint64_t offset = 0, const std::string& progressPrefix = {}) {
	for (uint64_t pos = 0, pos_ = stream.StreamSize(); pos < pos_;) {
		std::cout << std::format("{} Save: {:>6.02f}%: {} / {}\n", progressPrefix, pos * 100. / pos_, pos, pos_);
		const auto read = static_cast<size_t>(stream.ReadStreamPartial(pos, &buffer[0], buffer.size()));
		file.Write(offset, std::span(buffer).subspan(0, read));
		pos += read;
		offset += read;
	}
	return offset;
}

void StreamToFile(const Sqex::RandomAccessStream& stream, const std::filesystem::path& file) {
	std::vector<uint8_t> buffer(1048576 * 16);
	StreamToFile(stream, Utils::Win32::Handle::FromCreateFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS), buffer, 0, 
		std::format("[{}]", file.filename().string()));
}

void UnpackTtmp(const std::filesystem::path srcDir, const std::filesystem::path& dstDir) {
	auto ttmp = Sqex::ThirdParty::TexTools::TTMPL();
	from_json(Utils::ParseJsonFromFile(srcDir / "TTMPL.mpl"), ttmp);

	const auto ttmpd = std::make_shared<Sqex::FileRandomAccessStream>(srcDir / "TTMPD.mpd");
	const auto ttmpd2 = Utils::Win32::Handle::FromCreateFile(dstDir / "TTMPD.mpd", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS);

	uint64_t offset = 0;
	std::vector<uint8_t> buffer(1048576 * 16);
	ttmp.ForEachEntry([&](Sqex::ThirdParty::TexTools::ModEntry& entry) {
		const auto provider = ToDecompressedEntryProvider(std::make_shared<Sqex::Sqpack::RandomAccessStreamAsEntryProviderView>(entry.FullPath, ttmpd, entry.ModOffset, entry.ModSize));
		entry.ModSize = provider->StreamSize();

		if ((offset + entry.ModSize) / 4096 != offset / 4096)
			offset = Sqex::Align<uint64_t>(offset, 4096).Alloc;

		entry.ModOffset = offset;

		std::cout << std::format("{}: {} ({} bytes)\n", entry.FullPath, entry.ModOffset, entry.ModSize);
		offset = StreamToFile(*provider, ttmpd2, buffer, offset);

		return Sqex::ThirdParty::TexTools::TTMPL::TraverseCallbackResult::Continue;
		});

	nlohmann::json out;
	to_json(out, ttmp);
	std::ofstream tout(dstDir / "TTMPL.mpl");
	tout << out;
}

int main() {
	Utils::Win32::TpEnvironment tpenv;
	for (const auto& index : std::filesystem::recursive_directory_iterator(std::filesystem::path(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack.orig\)"))) {
		if (index.path().extension() != ".index")
			continue;

		tpenv.SubmitWork([path = index.path()]() {
			const auto expac = path.parent_path().filename().string();
			const auto dir = std::filesystem::path(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\)") / expac;
			std::filesystem::create_directories(dir);

			if (std::filesystem::exists(dir / path.filename()))
				return;

			Sqex::Sqpack::Creator creator(expac, std::filesystem::path(path).replace_extension("").replace_extension("").filename().string());
			creator.AddEntriesFromSqPack(path, true, true);

			const auto reader = Sqex::Sqpack::Reader(path);
			std::map<Sqex::Sqpack::SqIndex::LEDataLocator, Sqex::Sqpack::SqIndex::LEDataLocator> locatorMap;
			for (size_t i = 0; i < reader.EntryInfo.size(); ++i) {
				const auto& [srcLoc, entry] = reader.EntryInfo[i];
				if (!srcLoc.Value)
					continue;
				auto ep = reader.GetEntryProvider(entry.PathSpec, srcLoc, entry.Allocation);
				creator.AddEntry(ToDecompressedEntryProvider(ep));
			}

			const auto views = creator.AsViews(false);
			if (views.Entries.empty()) {
				std::filesystem::copy(path, dir / path.filename());
				std::filesystem::copy(std::filesystem::path(path).replace_extension(".index2"), dir / path.filename().replace_extension(".index2"));
				std::filesystem::copy(std::filesystem::path(path).replace_extension(".dat0"), dir / path.filename().replace_extension(".dat0"));
				return;
			}

			StreamToFile(*views.Index1, (dir / path.filename()).string() + ".tmp");
			StreamToFile(*views.Index2, (dir / path.filename().replace_extension(".index2")).string() + ".tmp");
			for (size_t i = 0; i < views.Data.size(); ++i)
				StreamToFile(*views.Data[i], (dir / path.filename().replace_extension(std::format(".dat{}", i))).string() + ".tmp");

			std::filesystem::rename((dir / path.filename()).string() + ".tmp", dir / path.filename());
			std::filesystem::rename((dir / path.filename().replace_extension(".index2")).string() + ".tmp", dir / path.filename().replace_extension(".index2"));
			for (size_t i = 0; i < views.Data.size(); ++i)
				std::filesystem::rename((dir / path.filename().replace_extension(std::format(".dat{}", i))).string() + ".tmp",
					dir / path.filename().replace_extension(std::format(".dat{}", i)));
		});
	}
	tpenv.WaitOutstanding();
	return 0;
}