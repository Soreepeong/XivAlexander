#include "pch.h"

#include <XivAlexanderCommon/Sqex/Model.h>
#include <XivAlexanderCommon/Sqex/Sqpack/BinaryEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Creator.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EmptyOrObfuscatedEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EntryRawStream.h>
#include <XivAlexanderCommon/Sqex/Sqpack/ModelEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/RandomAccessStreamAsEntryProviderView.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Reader.h>
#include <XivAlexanderCommon/Sqex/Sqpack/TextureEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Texture/ModifiableTextureStream.h>
#include <XivAlexanderCommon/Sqex/ThirdParty/TexTools.h>
#include <XivAlexanderCommon/Utils/Win32/ThreadPool.h>

std::shared_ptr<Sqex::Sqpack::EntryProvider> ToDecompressedEntryProvider(std::shared_ptr<Sqex::Sqpack::EntryProvider> src) {
	const auto header = src->ReadStream<Sqex::Sqpack::SqData::FileEntryHeader>(0);
	const auto& pathSpec = src->PathSpec();
	if (header.Type == Sqex::Sqpack::SqData::FileEntryType::EmptyOrObfuscated || header.DecompressedSize == 0)
		return src;

	auto raw = std::make_shared<Sqex::Sqpack::EntryRawStream>(std::move(src));

	switch (header.Type) {
		case Sqex::Sqpack::SqData::FileEntryType::Binary:
			return std::make_shared<Sqex::Sqpack::EmptyOrObfuscatedEntryProvider>(pathSpec, std::move(raw), header.DecompressedSize);

		case Sqex::Sqpack::SqData::FileEntryType::Model:
			return std::make_shared<Sqex::Sqpack::MemoryModelEntryProvider>(pathSpec, std::move(raw), Z_NO_COMPRESSION);

		case Sqex::Sqpack::SqData::FileEntryType::Texture:
			return std::make_shared<Sqex::Sqpack::MemoryTextureEntryProvider>(pathSpec, std::move(raw), Z_NO_COMPRESSION);
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

std::string DumpSqtexInfo(std::span<uint8_t> buf) {
	using namespace Sqex::Sqpack::SqData;
	std::stringstream ss;

	const auto& entryHeader = *reinterpret_cast<FileEntryHeader*>(&buf[0]);
	ss << std::format("HeaderSize={} Type={} Decompressed={} AllocUnit={} OccupiedUnit={} Blocks={}\n",
		entryHeader.HeaderSize.Value(), static_cast<int>(entryHeader.Type.Value()), entryHeader.DecompressedSize.Value(),
		entryHeader.AllocatedSpaceUnitCount.Value(), entryHeader.OccupiedSpaceUnitCount.Value(), entryHeader.BlockCountOrVersion.Value());
	const auto locators = span_cast<TextureBlockHeaderLocator>(buf, sizeof entryHeader, entryHeader.BlockCountOrVersion.Value());
	const auto subBlocks = span_cast<uint16_t>(buf, sizeof entryHeader + locators.size_bytes(), locators.back().FirstSubBlockIndex.Value() + locators.back().SubBlockCount.Value());
	const auto& texHeader = *reinterpret_cast<Sqex::Texture::Header*>(&buf[entryHeader.HeaderSize]);
	ss << std::format("Unk1={} Header={} Type={} Width={} Height={} Layers={} Mipmaps={}\n",
		texHeader.Unknown1.Value(), texHeader.HeaderSize.Value(), static_cast<uint32_t>(texHeader.Type.Value()), texHeader.Width.Value(), texHeader.Height.Value(), texHeader.Depth.Value(), texHeader.MipmapCount.Value());
	const auto mipmapOffsets = span_cast<uint32_t>(buf, entryHeader.HeaderSize + sizeof texHeader, texHeader.MipmapCount);

	ss << "Locators:\n";
	size_t lastOffset = 0;
	for (size_t i = 0; i < locators.size(); ++i) {
		const auto& l = locators[i];
		ss << std::format("\tOffset={} Size={} Decompressed={} Subblocks={}:{}\n", 
			l.FirstBlockOffset.Value(), l.TotalSize.Value(), l.DecompressedSize.Value(), l.FirstSubBlockIndex.Value(), l.FirstSubBlockIndex.Value() + l.SubBlockCount.Value());

		uint32_t baseRequestOffset = 0;
		if (i < mipmapOffsets.size()) {
			lastOffset = mipmapOffsets[i];
			ss << std::format("\tRequest={} (mipmapOffset)\n", lastOffset);
		} else if (i > 0) {
			ss << std::format("\tRequest={} (calc)\n", lastOffset);
		} else {
			lastOffset = 0;
			ss << std::format("\tRequest={} (first)\n", lastOffset);
		}
		auto pos = entryHeader.HeaderSize + l.FirstBlockOffset;
		for (auto j = l.FirstSubBlockIndex.Value(); j < l.FirstSubBlockIndex.Value() + l.SubBlockCount.Value(); ++j) {
			const auto& blockHeader = *reinterpret_cast<BlockHeader*>(&buf[pos]);
			ss << std::format("\t\tSub: Size={} HeaderSize={} Version={} Compressed={} Decompressed={}\n",
				subBlocks[j],
				blockHeader.HeaderSize.Value(), blockHeader.Version.Value(), blockHeader.CompressedSize.Value(), blockHeader.DecompressedSize.Value());
			pos += subBlocks[ j];
		}
		lastOffset += l.DecompressedSize;
	}

	return ss.str();
}

int main() {
	const auto sourcePath = std::filesystem::path(LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\)");
	const auto targetPath = std::filesystem::path(LR"(Z:\sqpacktest)");

	Utils::Win32::TpEnvironment tpenv(L"Sqpack Repacker");
	for (const auto& index : std::filesystem::recursive_directory_iterator(sourcePath)) {
		if (index.is_directory())
			continue;

		const auto path = index.path();
		const auto ext = path.extension().wstring();
		const auto copyToPath = targetPath / relative(index.path(), sourcePath);
		const auto copyToDir = copyToPath.parent_path();

		if (ext.starts_with(L".dat") || ext == L".index2")
			continue;

		if (exists(copyToPath) && std::filesystem::last_write_time(path) <= std::filesystem::last_write_time(copyToPath))
			continue;

		std::filesystem::create_directories(copyToPath.parent_path());

		if (ext == L".index") {
			tpenv.SubmitWork([path, copyToDir, copyToPath]() {
				const auto expac = path.parent_path().filename().string();
				const auto reader = Sqex::Sqpack::Reader(path);

				Sqex::Sqpack::Creator creator(expac, std::filesystem::path(path).replace_extension("").replace_extension("").filename().string());
				creator.AddEntriesFromSqPack(path, true, true);

				for (size_t i = 0; i < reader.EntryInfo.size(); ++i) {
					const auto& [locator, entry] = reader.EntryInfo[i];
					if (!locator.Value)
						continue;

					if (i % 512 == 0)
						std::cout << std::format("{:0>6}/{:0>6}_{}_{:08x}_{:08x}_{:08x}_{:08x}\n", i, reader.EntryInfo.size(), creator.DatName, locator.Value, entry.PathSpec.FullPathHash, entry.PathSpec.PathHash, entry.PathSpec.NameHash);

					creator.AddEntry(ToDecompressedEntryProvider(reader.GetEntryProvider(entry.PathSpec, locator, entry.Allocation)));
				}

				creator.WriteToFiles(copyToDir);
				std::filesystem::last_write_time(path, std::filesystem::last_write_time(copyToPath));
			});
			continue;
		}

		std::filesystem::copy_file(path, copyToPath, std::filesystem::copy_options::update_existing);
	}
	tpenv.WaitOutstanding();
	return 0;
}