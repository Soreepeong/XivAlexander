#include "pch.h"

#include <XivAlexanderCommon/Sqex/Sound/MusicImporter.h>
#include <XivAlexanderCommon/Sqex/Sound/Reader.h>
#include <XivAlexanderCommon/Sqex/Sqpack/Reader.h>
#include <XivAlexanderCommon/Utils/Win32/ThreadPool.h>
#include <XivAlexanderCommon/Utils/ZlibWrapper.h>

namespace Sqex::ZiPatch {
	static constexpr uint32_t FromChars(char c1 = 0, char c2 = 0, char c3 = 0, char c4 = 0) {
		return static_cast<uint32_t>(c1) << 24
			| static_cast<uint32_t>(c2) << 16
			| static_cast<uint32_t>(c3) << 8
			| static_cast<uint32_t>(c4) << 0;
	}

	struct Header {
		static const uint8_t Signature_Value[12];

		char Signature[12];
	};

	namespace Chunk {
		enum class TypeValues {
			AddDirectory = FromChars('A', 'D', 'I', 'R'),
			ApplyOption = FromChars('A', 'P', 'L', 'Y'),
			DeleteDirectory = FromChars('D', 'E', 'L', 'D'),
			EndOfFile = FromChars('E', 'O', 'F', '_'),
			FileHeader = FromChars('F', 'H', 'D', 'R'),
			Sqpk = FromChars('S', 'Q', 'P', 'K'),
		};

		struct ChunkHeader {
			Utils::BE<uint32_t> Size;
			Utils::BE<TypeValues> Type;
		};

		struct ChunkFooter {
			Utils::BE<uint32_t> Crc32;
		};

		struct AddDirectory : ChunkHeader {
			Utils::BE<uint32_t> DirNameSize;
			char DirName[1];
		};

		struct ApplyOption : ChunkHeader {
			enum class OptionType : uint32_t {
				IgnoreMissing = 1,
				IgnoreOldMismatch = 2,
			};

			Utils::BE<OptionType> Type;
			Utils::BE<uint32_t> Unknown_0x004;
			Utils::BE<uint32_t> Value;
		};

		struct DeleteDirectory : ChunkHeader {
			Utils::BE<uint32_t> DirNameSize;
			char DirName[1];
		};

		struct EndOfFile : ChunkHeader {
		};

		struct FileHeader : ChunkHeader {
			Utils::BE<uint16_t> Unknown_0x000;
			uint8_t Version;
			uint8_t Unknown_0x003;
			char PatchType[4];
			Utils::BE<uint32_t> EntryFiles;
		};

		struct FileHeaderV3 : ChunkHeader {
			Utils::BE<uint32_t> AddDirectories;
			Utils::BE<uint32_t> DeleteDirectories;
			Utils::BE<uint64_t> DeleteDataSize;
			Utils::BE<uint32_t> MinorVersion;
			Utils::BE<uint32_t> RepositoryName;
			Utils::BE<uint32_t> Commands;
			Utils::BE<uint32_t> SqpkAddCommands;
			Utils::BE<uint32_t> SqpkDeleteCommands;
			Utils::BE<uint32_t> SqpkExpandCommands;
			Utils::BE<uint32_t> SqpkHeaderCommands;
			Utils::BE<uint32_t> SqpkFileCommands;
		};

		enum class SqpkChunkTypeValues : uint32_t {
			FileAdd = FromChars('F', 'A'),
			FileRemoveAll = FromChars('F', 'R'),
			FileDelete = FromChars('F', 'D'),
			FileMakeTree = FromChars('F', 'M'),
			IndexAdd = FromChars('I', 'A'),
			IndexDelete = FromChars('I', 'D'),
			PatchInfo = FromChars('X', 0, 1),
			TargetInfo = FromChars('T'),
			DataAdd = FromChars('A'),
			DataDelete = FromChars('D'),
			DataExpand = FromChars('E', 'A'),
			DatHeaderVersion = FromChars('H', 'D', 'V'),
			DatHeaderSqpack = FromChars('H', 'D', 'D'),
			IndexHeaderVersion = FromChars('H', 'I', 'V'),
			IndexHeaderSqpack = FromChars('H', 'I', 'I'),
		};

		struct SqpkBase : ChunkHeader {
			Utils::BE<uint32_t> Size;
			Utils::BE<SqpkChunkTypeValues> SqpkChunkType;
		};

		struct SqpkFile : SqpkBase {
			Utils::BE<uint64_t> TargetOffset;
			Utils::BE<uint64_t> TargetSize;
			Utils::BE<uint32_t> PathSize;
			Utils::BE<uint16_t> ExpacId;
			Utils::BE<uint16_t> Padding_0x016;
			char Path[1];
		};

		enum class Platform : uint16_t {
			Win32 = 0,
			Ps3 = 1,
			Ps4 = 2,
		};

		extern const char PlatformNames[3][6];

		struct SqpkTargetInfo : SqpkBase {
			Utils::BE<Platform> Platform;
			Utils::BE<uint16_t> Region;
			Utils::BE<uint16_t> IsDebug;
			Utils::BE<uint16_t> Version;
			Utils::BE<uint64_t> DeletedDataSize;
			Utils::BE<uint64_t> SeekCount;
		};

		struct BaseSqpkTargetedCommand : SqpkBase {
			Utils::BE<uint16_t> MainId;
			union {
				uint8_t ExpacId;
				Utils::BE<uint16_t> SubId;
			};
			Utils::BE<uint32_t> FileId;
		};

		struct BaseSqpkDataTargetedCommand : BaseSqpkTargetedCommand {
			std::string ToPath(Platform platform) const {
				if (ExpacId)
					return std::format("sqpack/ex{}/{:02x}{:04x}.{}.dat{}", ExpacId, MainId.Value(), SubId.Value(), PlatformNames[static_cast<size_t>(platform)], FileId.Value());
				else
					return std::format("sqpack/ffxiv/{:02x}{:04x}.{}.dat{}", MainId.Value(), SubId.Value(), PlatformNames[static_cast<size_t>(platform)], FileId.Value());
			}
		};

		struct BaseSqpkIndexTargetedCommand : BaseSqpkTargetedCommand {
			std::string ToPath(Platform platform) const {
				if (ExpacId)
					return std::format("sqpack/ex{}/{:02x}{:04x}.{}.index", ExpacId, MainId.Value(), PlatformNames[static_cast<size_t>(platform)], SubId.Value());
				else
					return std::format("sqpack/ffxiv/{:02x}{:04x}.{}.index", MainId.Value(), PlatformNames[static_cast<size_t>(platform)], SubId.Value());
			}
		};

		struct SqpkDataAdd : BaseSqpkDataTargetedCommand {
			Utils::BE<uint32_t> TargetBlockIndex;
			Utils::BE<uint32_t> TargetDataBlockCount;
			Utils::BE<uint32_t> TargetClearBlockCount;
		};

		struct SqpkDataExpandDelete : BaseSqpkDataTargetedCommand {
			Utils::BE<uint32_t> TargetBlockIndex;
			Utils::BE<uint32_t> TargetDataBlockCount;
		};

		struct SqpkDatHeader : BaseSqpkDataTargetedCommand {
		};

		struct SqpkIndexHeader : BaseSqpkIndexTargetedCommand {
		};
	}
}

const uint8_t Sqex::ZiPatch::Header::Signature_Value[12]{ 0x91, 0x5a, 0x49, 0x50, 0x41, 0x54, 0x43, 0x48, 0x0d, 0x0a, 0x1a, 0x0a };
const char Sqex::ZiPatch::Chunk::PlatformNames[3][6]{
	"win32", "ps3\0\0", "ps4\0\0",
};

struct FilePart {
	static constexpr uint16_t SourceIndex_Zeros = UINT16_MAX;
	static constexpr uint16_t SourceIndex_EmptyBlock = UINT16_MAX - 1;

	uint64_t TargetOffset;
	uint64_t TargetSize;
	
	uint32_t SourceIndex;
	uint32_t SourceOffset;
	uint32_t SourceSize;
	uint32_t SplitFrom;

	uint32_t Crc32;
	uint32_t SourceIsDeflated : 1;
	uint32_t CrcAvailable : 1;
	uint32_t Reserved : 30;

	bool operator <(const FilePart& r) const {
		return TargetOffset < r.TargetOffset;
	}

	bool operator >(const FilePart& r) const {
		return TargetOffset > r.TargetOffset;
	}

	bool operator <(const uint64_t& r) const {
		return TargetOffset < r;
	}

	bool operator >(const uint64_t& r) const {
		return TargetOffset > r;
	}
};

void ReplaceBlock(std::vector<FilePart>& parts, const FilePart& part) {
	if (part.TargetSize == 0)
		return;
	for (const auto splitOffset : std::set<uint64_t>{ part.TargetOffset, part.TargetOffset + part.TargetSize }) {
		if (parts.empty()) {
			if (splitOffset)
				parts.emplace_back(FilePart{
					.TargetOffset = 0,
					.TargetSize = splitOffset,
					.SourceIndex = FilePart::SourceIndex_Zeros,
					});
			continue;
		}

		const auto endOffset = parts.back().TargetOffset + parts.back().TargetSize;
		auto i = std::lower_bound(parts.begin(), parts.end(), splitOffset);

		if (i == parts.end() && endOffset == splitOffset)
			continue;

		if (i == parts.end() && splitOffset > endOffset) {
			parts.emplace_back(FilePart{
				.TargetOffset = endOffset,
				.TargetSize = splitOffset - endOffset,
				.SourceIndex = FilePart::SourceIndex_Zeros,
				});
			continue;
		}

		if (i < parts.end() && i->TargetOffset == splitOffset)
			continue;

		if (i == parts.begin()) {
			if (splitOffset == 0) {
				parts.emplace(i, FilePart{
					.TargetOffset = 0,
					.TargetSize = parts.front().TargetOffset,
					});
			} else {
				i = parts.insert(i, FilePart{
					.TargetOffset = 0,
					.TargetSize = splitOffset,
					.SourceIndex = FilePart::SourceIndex_Zeros,
					});
				parts.emplace(i, FilePart{
					.TargetOffset = splitOffset,
					.TargetSize = parts[1].TargetOffset - splitOffset,
					.SourceIndex = FilePart::SourceIndex_Zeros,
					});
			}
		} else {
			const auto insertAt = i--;
			const auto prevTargetSize = i->TargetSize;
			i->TargetSize = splitOffset - i->TargetOffset;
			i->Crc32 = 0;
			i->CrcAvailable = 0;
			parts.emplace(insertAt, FilePart{
				.TargetOffset = splitOffset,
				.TargetSize = i->TargetOffset + prevTargetSize - splitOffset,
				.SourceIndex = i->SourceIndex,
				.SourceOffset = i->SourceOffset,
				.SourceSize = i->SourceSize,
				.SplitFrom = static_cast<uint32_t>(i->SplitFrom + splitOffset - i->TargetOffset),
				.SourceIsDeflated = i->SourceIsDeflated,
				});
		}
	}

	auto it1 = std::lower_bound(parts.begin(), parts.end(), part.TargetOffset);
	const auto it2 = std::lower_bound(parts.begin(), parts.end(), part.TargetOffset + part.TargetSize);

	if (it1 == parts.end() || it1->TargetOffset != part.TargetOffset)
		throw std::logic_error("A");

	if (it2 != parts.end() && it2->TargetOffset != part.TargetOffset + part.TargetSize)
		throw std::logic_error("B");
	else if (it2 == parts.end() && parts.back().TargetOffset + parts.back().TargetSize != part.TargetOffset + part.TargetSize)
		throw std::logic_error("C");

	*it1 = part;
	++it1;
	parts.erase(it1, it2);
}

class MergedFilePartStream : public Sqex::RandomAccessStream {
	const std::vector<std::shared_ptr<Sqex::RandomAccessStream>> m_files;
	const std::vector<FilePart> m_parts;

	mutable std::mutex m_inflaterMtx;
	mutable std::vector<std::unique_ptr<Utils::ZlibReusableInflater>> m_inflaters;

public:
	MergedFilePartStream(std::vector<std::shared_ptr<Sqex::RandomAccessStream>> files, std::vector<FilePart> parts)
		: m_files(std::move(files))
		, m_parts(std::move(parts))
		, m_inflaters(Utils::Win32::GetCoreCount()) {
	}

	[[nodiscard]] uint64_t StreamSize() const override {
		return m_parts.empty() ? 0 : m_parts.back().TargetOffset + m_parts.back().TargetSize;
	}

	uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
		if (m_parts.empty())
			return 0;

		std::unique_ptr<Utils::ZlibReusableInflater> inflater;
		const auto inflaterCleanup = Utils::CallOnDestruction([&]() {
			if (!inflater)
				return;
			const auto lock = std::lock_guard(m_inflaterMtx);
			for (auto& item : m_inflaters) {
				if (!item)
					item = std::move(inflater);
			}
			});

		auto out = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));
		auto relativeOffset = offset;

		auto it = std::lower_bound(m_parts.begin(), m_parts.end(), offset);
		if (it != m_parts.begin())
			--it;
		relativeOffset -= it->TargetOffset;

		for (; !out.empty() && it != m_parts.end(); ++it) {
			auto& part = *it;
			if (part.TargetSize <= relativeOffset) {
				relativeOffset -= part.TargetSize;
				continue;
			}

			if (part.SourceIndex == FilePart::SourceIndex_Zeros) {
				const auto available = std::min(out.size(), static_cast<size_t>(part.TargetSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

			} else if (part.SourceIndex == FilePart::SourceIndex_EmptyBlock) {
				uint8_t srcbuf[256]{};
				*reinterpret_cast<Sqex::Sqpack::SqData::FileEntryHeader*>(srcbuf) = {
						.HeaderSize = Sqex::EntryAlignment,
						.Type = Sqex::Sqpack::SqData::FileEntryType::None,
						.AllocatedSpaceUnitCount = part.SourceSize,
				};
				const auto src = std::span(srcbuf).subspan(part.SplitFrom, part.TargetSize).subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

			} else {
				auto& in = m_files[part.SourceIndex];

				if (part.SourceIsDeflated) {
					std::vector<uint8_t> buf2(part.SourceSize);
					in->ReadStream(part.SourceOffset, &buf2[0], buf2.size());

					if (!inflater) {
						const auto lock = std::lock_guard(m_inflaterMtx);
						for (auto& item : m_inflaters) {
							if (item) {
								inflater = std::move(item);
								break;
							}
						}
					}
					if (!inflater)
						inflater = std::make_unique<Utils::ZlibReusableInflater>(-MAX_WBITS);

					const auto src = (*inflater)(buf2)
						.subspan(part.SplitFrom, part.TargetSize)
						.subspan(static_cast<size_t>(relativeOffset));
					const auto available = std::min(out.size_bytes(), src.size_bytes());
					std::copy_n(src.begin(), available, out.begin());
					out = out.subspan(available);
					relativeOffset = 0;

				} else {
					const auto from = 0ULL + part.SourceOffset + part.SplitFrom + relativeOffset;
					const auto available = std::min<uint64_t>(part.TargetSize - relativeOffset, out.size_bytes());
					in->ReadStream(from, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;
				}
			}
		}

		return length - out.size_bytes();
	}
};

std::pair<std::vector<std::string>, std::map<std::string, std::vector<FilePart>>> LoadFileParts(const std::filesystem::path& patchFileIndexPath) {
	std::ifstream in2(patchFileIndexPath, std::ios::binary);

	std::vector<std::string> sourceFiles;
	do {
		sourceFiles.emplace_back(MAX_PATH, '\0');
		in2.read(sourceFiles.back().data(), MAX_PATH);
		sourceFiles.back().resize(strlen(sourceFiles.back().c_str()));
	} while (!sourceFiles.back().empty());
	sourceFiles.pop_back();

	std::map<std::string, std::vector<FilePart>> fileParts;
	while (true) {
		uint32_t nameLength = 0;
		in2.read(reinterpret_cast<char*>(&nameLength), sizeof nameLength);

		uint32_t partCount = 0;
		in2.read(reinterpret_cast<char*>(&partCount), sizeof partCount);

		if (nameLength == 0)
			break;

		char name[MAX_PATH]{};
		in2.read(name, nameLength);
		auto& parts = fileParts[name];

		parts.resize(partCount);
		in2.read(reinterpret_cast<char*>(&parts[0]), std::span(parts).size_bytes());
	}

	return std::make_pair(std::move(sourceFiles), std::move(fileParts));
}

void Update(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath, const std::filesystem::path& versionPath) {
	std::vector<std::filesystem::path> patchFiles;
	for (const auto& path : std::filesystem::directory_iterator(sourcePath)) {
		if (path.path().extension() != ".patch")
			continue;
		patchFiles.emplace_back(path.path());
	}
	std::sort(patchFiles.begin(), patchFiles.end(), [](const auto& l, const auto& r) { return 0 > wcscmp(l.filename().c_str() + 1, r.filename().c_str() + 1); });
	std::vector<std::shared_ptr<Sqex::RandomAccessStream>> patchFileStreams;
	for (const auto& patchFile : patchFiles)
		patchFileStreams.emplace_back(std::make_shared<Sqex::FileRandomAccessStream>(patchFile));

	std::map<std::string, std::vector<FilePart>> fileParts;
	for (uint16_t i = 0; i < patchFiles.size(); ++i) {
		const auto& patchFilePath = patchFiles[i];
		std::cout << std::format("{}\n", patchFilePath.string());
		std::ifstream in(patchFilePath, std::ios::binary);

		const auto patchFileIndexPath = std::filesystem::path(patchFilePath.wstring() + L".index");
		if (exists(patchFileIndexPath)) {
			std::vector<std::string> unused;
			std::tie(unused, fileParts) = LoadFileParts(patchFileIndexPath);

		} else {
			auto platform = Sqex::ZiPatch::Chunk::Platform::Win32;

			Sqex::ZiPatch::Header patchFileHeader{};
			in.read(reinterpret_cast<char*>(&patchFileHeader), sizeof patchFileHeader);
			if (0 != memcmp(patchFileHeader.Signature, Sqex::ZiPatch::Header::Signature_Value, sizeof Sqex::ZiPatch::Header::Signature_Value))
				throw std::runtime_error("bad zipatch signature");

			std::vector<char> chunk;
			for (bool end = false; !end && !in.eof();) {
				const auto chunkOffset = static_cast<uint64_t>(in.tellg());
				if (chunkOffset == UINT64_MAX)
					break;

				chunk.resize(sizeof Sqex::ZiPatch::Chunk::ChunkHeader);
				in.read(&chunk[0], chunk.size());
				const auto chunkSize = reinterpret_cast<Sqex::ZiPatch::Chunk::ChunkHeader*>(&chunk[0])->Size.Value();
				const auto chunkType = reinterpret_cast<Sqex::ZiPatch::Chunk::ChunkHeader*>(&chunk[0])->Type.Value();
				const auto chunkFooterOffset = chunkOffset + sizeof Sqex::ZiPatch::Chunk::ChunkHeader + chunkSize;
				const auto chunkEndOffset = chunkFooterOffset + sizeof Sqex::ZiPatch::Chunk::ChunkFooter;

				switch (chunkType) {
					case Sqex::ZiPatch::Chunk::TypeValues::AddDirectory:
					{
						chunk.reserve(sizeof Sqex::ZiPatch::Chunk::AddDirectory + MAX_PATH);
						chunk.resize(sizeof Sqex::ZiPatch::Chunk::AddDirectory);
						in.seekg(chunkOffset, std::ios::beg);
						in.read(&chunk[0], chunk.size());

						const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::AddDirectory*>(&chunk[0]);
						chunk.resize(sizeof Sqex::ZiPatch::Chunk::AddDirectory + data.DirNameSize);
						in.seekg(chunkOffset, std::ios::beg);
						in.read(&chunk[0], chunk.size());

						std::cout << std::format("AddDirectory: {}\n", std::string(data.DirName, data.DirNameSize - 1));
						break;
					}

					case Sqex::ZiPatch::Chunk::TypeValues::ApplyOption:
						break;

					case Sqex::ZiPatch::Chunk::TypeValues::DeleteDirectory:
					{
						chunk.reserve(sizeof Sqex::ZiPatch::Chunk::DeleteDirectory + MAX_PATH);
						chunk.resize(sizeof Sqex::ZiPatch::Chunk::DeleteDirectory);
						in.seekg(chunkOffset, std::ios::beg);
						in.read(&chunk[0], chunk.size());

						const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::DeleteDirectory*>(&chunk[0]);
						chunk.resize(sizeof Sqex::ZiPatch::Chunk::DeleteDirectory + data.DirNameSize);
						in.seekg(chunkOffset, std::ios::beg);
						in.read(&chunk[0], chunk.size());

						std::cout << std::format("DeleteDirectory: {}\n", std::string(data.DirName, data.DirNameSize - 1));
						break;
					}

					case Sqex::ZiPatch::Chunk::TypeValues::EndOfFile:
						end = true;
						break;

					case Sqex::ZiPatch::Chunk::TypeValues::FileHeader:
					{
						in.seekg(chunkOffset, std::ios::beg);
						chunk.resize(sizeof Sqex::ZiPatch::Chunk::FileHeader);
						in.read(&chunk[0], chunk.size());
						const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::FileHeader*>(&chunk[0]);
						break;
					}

					case Sqex::ZiPatch::Chunk::TypeValues::Sqpk:
					{
						in.seekg(chunkOffset, std::ios::beg);
						chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkBase);
						in.read(&chunk[0], chunk.size());
						const auto sqpkChunkType = reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkBase*>(&chunk[0])->SqpkChunkType.Value();
						switch (sqpkChunkType) {
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::FileAdd:
							{
								chunk.reserve(sizeof Sqex::ZiPatch::Chunk::SqpkFile + MAX_PATH);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkFile);
								in.seekg(chunkOffset, std::ios::beg);
								in.read(&chunk[0], chunk.size());

								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkFile*>(&chunk[0]);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkFile + data.PathSize);
								in.seekg(chunkOffset, std::ios::beg);
								in.read(&chunk[0], chunk.size());

								const auto path = std::string(data.Path, data.PathSize - 1);
								auto& parts = fileParts[path];

								in.seekg(chunkOffset + offsetof(Sqex::ZiPatch::Chunk::SqpkFile, Path) + data.PathSize, std::ios::beg);
								Sqex::Sqpack::SqData::BlockHeader blockHeader{};
								if (data.TargetOffset == 0)
									parts.clear();
								for (uint64_t targetOffset = data.TargetOffset; targetOffset < data.TargetOffset + data.TargetSize; targetOffset += blockHeader.DecompressedSize) {
									const auto currentOffset = static_cast<uint32_t>(in.tellg());
									in.read(reinterpret_cast<char*>(&blockHeader), sizeof blockHeader);
									const auto blockDataSize = blockHeader.CompressedSize == Sqex::Sqpack::SqData::BlockHeader::CompressedSizeNotCompressed ? blockHeader.DecompressedSize : blockHeader.CompressedSize;
									const auto align = Sqex::Align(blockHeader.HeaderSize + blockDataSize);
									in.seekg(align.Alloc - sizeof blockHeader, std::ios::cur);
									
									if (blockHeader.CompressedSize == Sqex::Sqpack::SqData::BlockHeader::CompressedSizeNotCompressed) {
										ReplaceBlock(parts, FilePart{
											.TargetOffset = targetOffset,
											.TargetSize = blockHeader.DecompressedSize,
											.SourceIndex = i,
											.SourceOffset = currentOffset + blockHeader.HeaderSize,
											.SourceSize = blockHeader.DecompressedSize,
											});
									} else {
										ReplaceBlock(parts, FilePart{
											.TargetOffset = targetOffset,
											.TargetSize = blockHeader.DecompressedSize,
											.SourceIndex = i,
											.SourceOffset = currentOffset + blockHeader.HeaderSize,
											.SourceSize = blockHeader.CompressedSize,
											.SourceIsDeflated = 1,
											});
									}
								}
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::FileRemoveAll:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkFile + MAX_PATH);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkFile*>(&chunk[0]);

								const auto expacName = data.ExpacId == 0 ? std::string("ffxiv") : std::format("ex{}", data.ExpacId.Value());
								for (auto it = fileParts.begin(); it != fileParts.end(); ) {
									if (it->first.starts_with(std::format("sqpack/{}/", expacName)))
										it = fileParts.erase(it);
									if (it->first.starts_with(std::format("movie/{}/", expacName)))
										it = fileParts.erase(it);
								}
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::FileDelete:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkFile + MAX_PATH);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkFile*>(&chunk[0]);
								const auto path = std::string(data.Path, data.PathSize.Value() - 1);
								std::cout << std::format("FileDelete: {}\n", path);
								fileParts.erase(path);
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::FileMakeTree:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkFile + MAX_PATH);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkFile*>(&chunk[0]);
								std::cout << std::format("FileMakeTree: {}\n", std::string(data.Path, data.PathSize - 1));
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::IndexAdd:
								break;
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::IndexDelete:
								break;
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::PatchInfo:
								break;
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::TargetInfo:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkTargetInfo);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkTargetInfo*>(&chunk[0]);
								platform = data.Platform.Value();
								std::cout << std::format("{}: {}\n", patchFilePath, Sqex::ZiPatch::Chunk::PlatformNames[static_cast<size_t>(platform)]);
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DataAdd:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkDataAdd);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkDataAdd*>(&chunk[0]);
								const auto path = data.ToPath(platform);
								auto& parts = fileParts[path];
								ReplaceBlock(parts, FilePart{
									.TargetOffset = data.TargetBlockIndex * Sqex::EntryAlignment,
									.TargetSize = data.TargetDataBlockCount * Sqex::EntryAlignment,
									.SourceIndex = i,
									.SourceOffset = static_cast<uint32_t>(in.tellg()),
									.SourceSize = data.TargetDataBlockCount * Sqex::EntryAlignment,
									});
								if (data.TargetClearBlockCount) {
									ReplaceBlock(parts, FilePart{
										.TargetOffset = (data.TargetBlockIndex + data.TargetDataBlockCount) * Sqex::EntryAlignment,
										.TargetSize = data.TargetClearBlockCount * Sqex::EntryAlignment,
										.SourceIndex = FilePart::SourceIndex_Zeros,
										.SourceSize = data.TargetClearBlockCount - 1,
										});
								}
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DataDelete:
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DataExpand:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkDataExpandDelete);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkDataExpandDelete*>(&chunk[0]);
								const auto path = data.ToPath(platform);
								auto& parts = fileParts[path];
								ReplaceBlock(parts, FilePart{
									.TargetOffset = data.TargetBlockIndex * Sqex::EntryAlignment,
									.TargetSize = Sqex::EntryAlignment,
									.SourceIndex = FilePart::SourceIndex_EmptyBlock,
									.SourceSize = data.TargetDataBlockCount - 1,
									});
								if (data.TargetDataBlockCount) {
									ReplaceBlock(parts, FilePart{
										.TargetOffset = (data.TargetBlockIndex + 1) * Sqex::EntryAlignment,
										.TargetSize = (data.TargetDataBlockCount - 1) * Sqex::EntryAlignment,
										.SourceIndex = FilePart::SourceIndex_Zeros,
										});
								}
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DatHeaderVersion:
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DatHeaderSqpack:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkDatHeader);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkDatHeader*>(&chunk[0]);
								const auto path = data.ToPath(platform);
								auto& parts = fileParts[path];
								ReplaceBlock(parts, FilePart{
									.TargetOffset = sqpkChunkType == Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DatHeaderVersion ? 0ULL : 1024ULL,
									.TargetSize = 1024,
									.SourceIndex = i,
									.SourceOffset = static_cast<uint32_t>(in.tellg()),
									.SourceSize = 1024,
									});
								break;
							}
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::IndexHeaderVersion:
							case Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::IndexHeaderSqpack:
							{
								in.seekg(chunkOffset, std::ios::beg);
								chunk.resize(sizeof Sqex::ZiPatch::Chunk::SqpkIndexHeader);
								in.read(&chunk[0], chunk.size());
								const auto& data = *reinterpret_cast<Sqex::ZiPatch::Chunk::SqpkDatHeader*>(&chunk[0]);
								const auto path = data.ToPath(platform);
								auto& parts = fileParts[path];
								ReplaceBlock(parts, FilePart{
									.TargetOffset = sqpkChunkType == Sqex::ZiPatch::Chunk::SqpkChunkTypeValues::DatHeaderVersion ? 0ULL : 1024ULL,
									.TargetSize = 1024,
									.SourceIndex = i,
									.SourceOffset = static_cast<uint32_t>(in.tellg()),
									.SourceSize = 1024,
									});
								break;
							}

							default:
								std::cout << std::format("Unknown sqpk chunk type {}{}{}{} ({:08x})\n",
									static_cast<char>(static_cast<uint32_t>(sqpkChunkType) >> 24),
									static_cast<char>(static_cast<uint32_t>(sqpkChunkType) >> 16),
									static_cast<char>(static_cast<uint32_t>(sqpkChunkType) >> 8),
									static_cast<char>(static_cast<uint32_t>(sqpkChunkType) >> 0),
									static_cast<uint32_t>(sqpkChunkType)
								);
						}
						break;
					}

					default:
						std::cout << std::format("Unknown chunk type {}{}{}{} ({:08x})\n",
							static_cast<char>(static_cast<uint32_t>(chunkType) >> 24),
							static_cast<char>(static_cast<uint32_t>(chunkType) >> 16),
							static_cast<char>(static_cast<uint32_t>(chunkType) >> 8),
							static_cast<char>(static_cast<uint32_t>(chunkType) >> 0),
							static_cast<uint32_t>(chunkType)
						);
				}

				in.seekg(chunkEndOffset, std::ios::beg);
			}

			{
				const auto tmpPath = std::filesystem::path(patchFileIndexPath.wstring() + L".tmp");
				std::ofstream out(tmpPath, std::ios::binary);

				char patchFileName[MAX_PATH]{};
				for (auto j = 0; j < i; ++j) {
					strncpy_s(patchFileName, patchFiles[j].filename().string().c_str(), MAX_PATH);
					out.write(patchFileName, MAX_PATH);
				}
				memset(patchFileName, 0, sizeof patchFileName);
				out.write(patchFileName, MAX_PATH);

				std::vector<uint8_t> buf;
				for (auto& [path, parts] : fileParts) {
					const auto stream = std::make_shared<MergedFilePartStream>(patchFileStreams, parts);
					for (auto& part : parts) {
						if (part.SourceIndex == FilePart::SourceIndex_EmptyBlock || part.SourceIndex == FilePart::SourceIndex_Zeros)
							continue;
						if (part.CrcAvailable)
							continue;
						buf.resize(part.TargetSize);
						stream->ReadStream(part.TargetOffset, std::span(buf));
						part.Crc32 = crc32_z(0, &buf[0], buf.size());
						part.CrcAvailable = 1;
					}
					uint32_t val = static_cast<uint32_t>(path.size());
					out.write(reinterpret_cast<const char*>(&val), sizeof val);
					val = static_cast<uint32_t>(parts.size());
					out.write(reinterpret_cast<const char*>(&val), sizeof val);
					out.write(path.c_str(), path.size());
					out.write(reinterpret_cast<const char*>(&parts[0]), std::span(parts).size_bytes());
				}
				out.write("\0\0\0\0\0\0\0\0", 8);

				out.close();
				rename(tmpPath, patchFileIndexPath);
			}
		}

		/*
		{
			std::map<std::string, std::shared_ptr<MergedFilePartStream>> mergedStreams;
			for (const auto& [path, parts] : fileParts)
				mergedStreams.emplace(path, std::make_shared<MergedFilePartStream>(patchFileStreams, parts));

			Utils::Win32::TpEnvironment tpEnv(L"ZiPatchExtractor", 1);
			for (const auto& [path, stream] : mergedStreams) {
				tpEnv.SubmitWork([&]() {
					const auto targetFilePath = targetPath / path;

					// std::cout << std::format("{}\n", targetFilePath);
					std::filesystem::create_directories(targetFilePath.parent_path());
					std::ofstream out(targetFilePath, std::ios::binary);

					if (targetFilePath.extension() != ".index")
						return;

					const auto index2Path = std::filesystem::path(path).replace_extension(".index2").string();
					std::vector<std::shared_ptr<Sqex::RandomAccessStream>> dataStreams;
					for (int j = 0; j < 8; ++j) {
						const auto dataPath = Utils::StringReplaceAll<std::string>(std::filesystem::path(path).replace_extension(std::format(".dat{}", j)).string(), "\\", "/");
						try {
							dataStreams.emplace_back(mergedStreams.at(dataPath));
						} catch (const std::out_of_range&) {
							break;
						}
					}

					if (!std::any_of(fileParts[path].begin(), fileParts[path].end(), [&](const auto& e) {
						return e.SourceIndex == i;
						})) {

						if (!std::any_of(fileParts[index2Path].begin(), fileParts[index2Path].end(), [&](const auto& e) {
							return e.SourceIndex == i;
							})) {

							bool changed = false;
							for (size_t j = 0; !changed && j < dataStreams.size(); ++j) {
								const auto dataPath = Utils::StringReplaceAll<std::string>(std::filesystem::path(path).replace_extension(std::format(".dat{}", j)).string(), "\\", "/");
								changed |= std::any_of(fileParts[dataPath].begin(), fileParts[dataPath].end(), [&](const auto& e) {
									return e.SourceIndex == i;
									});
							}
							if (!changed)
								return;
						}
					}

					auto reader = Sqex::Sqpack::Reader(mergedStreams.at(path), mergedStreams.at(index2Path), std::move(dataStreams));
					std::ofstream filelist(std::filesystem::path(targetFilePath).replace_extension(".csv"), std::ios::app);
					if (filelist.tellp() == 0)
						filelist << "Path Hash,Name Hash,Full Path Hash,Full Path,Dat File Index,Dat File Offset,Allocation,Type,Decompressed Size,Patch\n";

					const auto versionName = std::filesystem::path(patchFilePath).replace_extension("").filename().string().substr(1);

					for (const auto& [locator, entryInfo] : reader.EntryInfo) {
						const auto dataPath = Utils::StringReplaceAll<std::string>(std::filesystem::path(path).replace_extension(std::format(".dat{}", locator.DatFileIndex)).string(), "\\", "/");
						if (!std::any_of(fileParts[dataPath].begin(), fileParts[dataPath].end(), [&](const auto& e) {
							return e.SourceIndex == i && e.TargetOffset + e.TargetSize > locator.DatFileOffset() && locator.DatFileOffset() + entryInfo.Allocation > e.TargetOffset;
							}))
							continue;

						const auto entryProvider = reader.GetEntryProvider(entryInfo.PathSpec, locator, entryInfo.Allocation);
						const auto header = entryProvider->ReadStream<Sqex::Sqpack::SqData::FileEntryHeader>(0);

						if (!std::any_of(fileParts[dataPath].begin(), fileParts[dataPath].end(), [&](const auto& e) {
							return e.SourceIndex == i && e.TargetOffset + e.TargetSize > locator.DatFileOffset() && locator.DatFileOffset() + header.GetTotalEntrySize() > e.TargetOffset;
							}))
							continue;

						const char* typeName = "Unknown";
						switch (header.Type.Value()) {
							case Sqex::Sqpack::SqData::FileEntryType::None:
								typeName = "None";
								break;
							case Sqex::Sqpack::SqData::FileEntryType::EmptyOrObfuscated:
								typeName = "Placeholder";
								break;
							case Sqex::Sqpack::SqData::FileEntryType::Binary:
								typeName = "Binary";
								break;
							case Sqex::Sqpack::SqData::FileEntryType::Model:
								typeName = "Model";
								break;
							case Sqex::Sqpack::SqData::FileEntryType::Texture:
								typeName = "Texture";
								break;
						}

						filelist << std::format("{:08x},{:08x},{:08x},{},{},{},{},{},{},{}\n",
							entryInfo.PathSpec.PathHash,
							entryInfo.PathSpec.NameHash,
							entryInfo.PathSpec.FullPathHash,
							entryInfo.PathSpec.FullPath.string(),
							locator.DatFileIndex,
							locator.DatFileOffset(),
							entryInfo.Allocation,
							typeName,
							header.DecompressedSize.Value(),
							versionName
						);
					}

					//std::vector<char> buf;
					//Sqex::Align<uint64_t>(stream->StreamSize(), 64 * 1048576).IterateChunked([&](uint64_t, const uint64_t offset, const uint64_t length) {
					//	buf.resize(static_cast<size_t>(length));
					//	stream->ReadStream(offset, std::span(buf));
					//	out.write(&buf[0], buf.size());
					//	});
					});
			}
			tpEnv.WaitOutstanding();
		}
		//*/
	}

	// std::ofstream(targetPath / versionPath) << std::filesystem::path(patchFiles.back()).replace_extension("").filename().string().substr(1);
	// std::ofstream((targetPath / versionPath).replace_extension(".bck")) << std::filesystem::path(patchFiles.back()).replace_extension("").filename().string().substr(1);
}

void Verify(const std::filesystem::path& patchFileIndexPath, const std::filesystem::path& targetPath) {
	const auto [patchFileNames, fileParts] = LoadFileParts(patchFileIndexPath);
	std::vector<char> buf;
	for (const auto& [pathStr, parts] : fileParts) {
		const auto path = targetPath / pathStr;
		std::cout << std::format("Checking {}...\n", pathStr);
		
		std::ifstream in(path, std::ios::binary);
		for (const auto& part : parts) {
			buf.resize(part.TargetSize);
			in.read(&buf[0], buf.size());

			if (part.CrcAvailable) {
				const auto cz = crc32_z(0, reinterpret_cast<uint8_t*>(&buf[0]), buf.size());
				if (part.Crc32 != cz)
					std::cout << std::format("{}~{} bad crc\n", part.TargetOffset, part.TargetOffset + part.TargetSize);

			} else if (part.SourceIndex == FilePart::SourceIndex_Zeros) {
				if (std::any_of(buf.begin(), buf.end(), [](const auto& c) { return !!c; }))
					std::cout << std::format("{}~{} not all zero\n", part.TargetOffset, part.TargetOffset + part.TargetSize);

			} else if (part.SourceIndex == FilePart::SourceIndex_EmptyBlock) {
				uint8_t srcbuf[256]{};
				*reinterpret_cast<Sqex::Sqpack::SqData::FileEntryHeader*>(srcbuf) = {
						.HeaderSize = Sqex::EntryAlignment,
						.Type = Sqex::Sqpack::SqData::FileEntryType::None,
						.AllocatedSpaceUnitCount = part.SourceSize,
				};
				const auto src = std::span(srcbuf).subspan(part.SplitFrom, part.TargetSize);
				if (0 != memcmp(&src[0], &buf[0], buf.size()))
					std::cout << std::format("{}~{} bad empty block\n", part.TargetOffset, part.TargetOffset + part.TargetSize);
			}
		}
	}
}

int main() {
	Update(LR"(Z:\patch-dl.ffxiv.com\boot\2b5cbc63)", LR"(C:\Temp\ffxivtest\boot)", LR"(ffxivboot.ver)");
	Verify(LR"(Z:\patch-dl.ffxiv.com\boot\2b5cbc63\D2021.11.16.0000.0001.patch.index)", LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\boot)");
	
	Update(LR"(Z:\patch-dl.ffxiv.com\game\4e9a232b)", LR"(C:\Temp\ffxivtest\game)", LR"(ffxivgame.ver)");
	Verify(LR"(Z:\patch-dl.ffxiv.com\game\4e9a232b\D2022.01.25.0000.0000.patch.index)", LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	
	Update(LR"(Z:\patch-dl.ffxiv.com\game\ex1\6b936f08)", LR"(C:\Temp\ffxivtest\game)", LR"(sqpack\ex1\ex1.ver)");
	Verify(LR"(Z:\patch-dl.ffxiv.com\game\ex1\6b936f08\D2021.11.21.0000.0000.patch.index)", LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	
	Update(LR"(Z:\patch-dl.ffxiv.com\game\ex2\f29a3eb2)", LR"(C:\Temp\ffxivtest\game)", LR"(sqpack\ex2\ex2.ver)");
	Verify(LR"(Z:\patch-dl.ffxiv.com\game\ex2\f29a3eb2\D2021.12.14.0000.0000.patch.index)", LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	
	Update(LR"(Z:\patch-dl.ffxiv.com\game\ex3\859d0e24)", LR"(C:\Temp\ffxivtest\game)", LR"(sqpack\ex3\ex3.ver)");
	Verify(LR"(Z:\patch-dl.ffxiv.com\game\ex3\859d0e24\D2021.12.14.0000.0000.patch.index)", LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	
	Update(LR"(Z:\patch-dl.ffxiv.com\game\ex4\1bf99b87)", LR"(C:\Temp\ffxivtest\game)", LR"(sqpack\ex4\ex4.ver)");
	Verify(LR"(Z:\patch-dl.ffxiv.com\game\ex4\1bf99b87\D2022.01.18.0000.0000.patch.index)", LR"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
}
