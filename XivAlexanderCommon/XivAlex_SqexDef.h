// ReSharper disable CppNonExplicitConversionOperator
// ReSharper disable CppNonExplicitConvertingConstructor
// ReSharper disable CppClangTidyCppcoreguidelinesProTypeMemberInit
#pragma once

#include <algorithm>
#include <stdexcept>
#include <span>
#include <type_traits>
#include "Utils_Win32_Handle.h"

#pragma warning(push)
#pragma warning(disable: 26495)

namespace XivAlex::SqexDef {

	enum class SqexLanguage {
		Undefined = 0,
		Japanese = 1,
		English = 2,
		German = 3,
		French = 4,
		ChineseSimplified = 5,
		ChineseTraditional = 6,
		Korean = 7,
	};

	template<typename T>
	struct LE {
	private:
		union {
			T value;
		};

	public:
		LE() = default;

		LE(T defaultValue)
			: value(defaultValue) {
		}

		operator T() const {
			return Value();
		}

		LE<T>& operator= (T newValue) {
			Value(std::move(newValue));
			return *this;
		}
		
		T Value() const {
			return value;
		}
		
		void Value(T newValue) {
			value = std::move(newValue);
		}
	};

	template<typename T>
	struct BE {
	private:
		union {
			T value;
			char buf[sizeof T];
		};

	public:
		BE() = default;

		BE(T defaultValue)
			: value(defaultValue) {
			std::reverse(buf, buf + sizeof T);
		}

		operator T() const {
			return Value();
		}

		T Value() const {
			union {
				T localval;
				char localbuf[sizeof T];
			};
			memcpy(localbuf, buf, sizeof T);
			std::reverse(localbuf, localbuf + sizeof T);
			return localval;
		}

		template<std::enable_if_t<std::is_enum_v<T>>>
		void Value(T newValue) {
			value = std::move(newValue);
			std::reverse(buf, buf + sizeof buf);
		}

		template<std::enable_if_t<!std::is_enum_v<T>>>

		void Value(T newValue) {
			value = std::move(newValue);
			std::reverse(buf, buf + sizeof buf);
		}
	};

	class InvalidSqpackException : public std::runtime_error {
	public:
		using std::runtime_error::runtime_error;
	};

	enum class SqpackType : uint32_t {
		Unspecified = UINT32_MAX,
		SqDatabase = 0,
		SqData = 1,
		SqIndex = 2,
	};

	struct SqpackHeader {
		static constexpr uint32_t Unknown1_Value = 1;
		static constexpr uint32_t Unknown2_Value = 0xFFFFFFFFUL;
		static const char Signature_Value[12];

		char Signature[12]{};
		LE<uint32_t> HeaderSize;
		LE<uint32_t> Unknown1;  // 1
		LE<SqpackType> Type;
		LE<uint32_t> YYYYMMDD;
		LE<uint32_t> Time;
		LE<uint32_t> Unknown2; // Intl: 0xFFFFFFFF, KR/CN: 1
		char Padding_0x024[0x3c0 - 0x024];
		char Sha1[20];
		char Padding_0x3D4[0x2c];

		void VerifySqpackHeader(SqpackType supposedType);
	};
	static_assert(offsetof(SqpackHeader, Sha1) == 0x3c0, "Bad SqpackHeader definition");
	static_assert(sizeof(SqpackHeader) == 1024);

	static constexpr uint32_t EntryAlignment = 128;

	namespace SqIndex {
		struct SegmentDescriptor {
			LE<uint32_t> Count;
			LE<uint32_t> Offset;
			LE<uint32_t> Size;
			char Sha1[20];
			char Padding_0x020[0x28];
		};
		static_assert(sizeof SegmentDescriptor == 0x48);

		/*
		 * Segment 1
		 * * Stands for files
		 * * Descriptor.Count = 1
		 *
		 * Segment 2
		 * * Descriptor.Count stands for number of .dat files
		 * * Descriptor.Size is always 0x100
		 * * Data is always 8x00s, 4xFFs, and the rest is 0x00s
		 *
		 * Segment 3
		 * * Descriptor.Count = 0
		 *
		 * Segment 4
		 * * Stands for folders
		 * * Descriptor.Count = 0
		 */

		struct Header {
			enum class IndexType : uint32_t {
				Unspecified = UINT32_MAX,
				Index = 0,
				Index2 = 2,
			};

			LE<uint32_t> HeaderSize;
			SegmentDescriptor FileSegment;
			char Padding_0x04C[4];
			SegmentDescriptor DataFilesSegment;  // Size is always 0x100
			SegmentDescriptor UnknownSegment3;
			SegmentDescriptor FolderSegment;
			char Padding_0x128[4];
			LE<IndexType> Type;
			char Padding_0x130[0x3c0 - 0x130];
			char Sha1[20];
			char Padding_0x3D4[0x2c];

			void VerifySqpackIndexHeader(IndexType expectedIndexType);

			void VerifyDataFileSegment(const std::vector<char>& DataFileSegment);

		};
		static_assert(sizeof(Header) == 1024);

		struct LEDataLocator : LE<uint32_t> {
			using LE<uint32_t>::LE;
			LEDataLocator(uint32_t index, uint64_t offset);

			[[nodiscard]] uint32_t Index() const;
			[[nodiscard]] uint64_t Offset() const;
			uint32_t Index(uint32_t value);
			uint64_t Offset(uint64_t value);
		};

		struct FileSegmentEntry {
			LE<uint32_t> NameHash;
			LE<uint32_t> PathHash;
			LEDataLocator DatFile;
			LE<uint32_t> Padding;
		};

		struct FileSegmentEntry2 {
			LE<uint32_t> FullPathHash;
			LEDataLocator DatFile;
		};

		struct Segment3Entry {
			LE<uint32_t> Unknown1;
			LE<uint32_t> Unknown2;
			LE<uint32_t> Unknown3;
			LE<uint32_t> Unknown4;
		};

		struct FolderSegmentEntry {
			LE<uint32_t> NameHash;
			LE<uint32_t> FileSegmentOffset;
			LE<uint32_t> FileSegmentSize;
			LE<uint32_t> Padding;

			void Verify() const;
		};
	}

	namespace SqData {
		struct Header {
			static constexpr uint32_t MaxFileSize_Value = 0x77359400;  // 2GB
			static constexpr uint64_t MaxFileSize_MaxValue = 0x800000000ULL;  // Max addressable via how OffsetAfterHeaders works
			static constexpr uint32_t Unknown1_Value = 0x10;

			LE<uint32_t> HeaderSize;
			LE<uint32_t> Null1;
			LE<uint32_t> Unknown1;
			union DataSizeDivBy8Type {
				LE<uint32_t> RawValue;

				DataSizeDivBy8Type& operator=(uint64_t value) {
					if (value % 128)
						throw std::invalid_argument("Value must be a multiple of 8.");
					if (value / 128ULL > UINT32_MAX)
						throw std::invalid_argument("Value too big.");
					RawValue = static_cast<uint32_t>(value / 128ULL);
					return *this;
				}

				operator uint64_t() const {
					return Value();
				}

				[[nodiscard]] uint64_t Value() const {
					return RawValue * 128ULL;
				}
			} DataSize;  // From end of this header to EOF
			LE<uint32_t> SpanIndex;  // 0x01 = .dat0, 0x02 = .dat1, 0x03 = .dat2, ...
			LE<uint32_t> Null2;
			LE<uint64_t> MaxFileSize;
			char DataSha1[20];  // From end of this header to EOF
			char Padding_0x034[0x3c0 - 0x034];
			char Sha1[20];
			char Padding_0x3D4[0x2c];

			void Verify(uint32_t expectedSpanIndex) const;
		};
		static_assert(offsetof(Header, Sha1) == 0x3c0, "Bad SqDataHeader definition");

		enum class FileEntryType {
			Empty = 1,
			Binary = 2,
			Model = 3,
			Texture = 4,
		};

		struct BlockHeaderLocator {
			LE<uint32_t> Offset;
			LE<uint16_t> BlockSize;
			LE<uint16_t> DecompressedDataSize;
		};

		struct BlockHeader {
			static constexpr uint32_t CompressedSizeNotCompressed = 32000;
			LE<uint32_t> HeaderSize;
			LE<uint32_t> Version;
			LE<uint32_t> CompressedSize;
			LE<uint32_t> DecompressedSize;
		};

		struct FileEntryHeader {
			LE<uint32_t> HeaderSize;
			LE<FileEntryType> Type;
			LE<uint32_t> DecompressedSize;
			LE<uint32_t> Unknown1;
			LE<uint32_t> BlockBufferSize;
			union {
				LE<uint32_t> BlockCount;  // Valid for Type 1, 2, 4
				LE<uint32_t> Version;  // Valid for Type 3
			};
		};

		struct TextureBlockHeaderLocator {
			LE<uint32_t> FirstBlockOffset;
			LE<uint32_t> TotalSize;
			LE<uint32_t> DecompressedSize;
			LE<uint32_t> FirstSubBlockIndex;
			LE<uint32_t> SubBlockCount;
		};

		struct TexHeader {
			LE<uint16_t> Unknown1;
			LE<uint16_t> HeaderSize;
			LE<uint32_t> CompressionType;
			LE<uint16_t> DecompressedWidth;
			LE<uint16_t> DecompressedHeight;
			LE<uint16_t> Depth;
			LE<uint16_t> MipmapCount;
			char Unknown2[0xb];
		};

		struct ModelBlockLocator {
			template<typename T>
			struct ChunkInfo {
				T Stack;
				T Runtime;
				T Vertex[3];
				T EdgeGeometryVertex[3];
				T Index[3];
			};

			ChunkInfo<LE<uint32_t>> DecompressedSizes;
			ChunkInfo<LE<uint32_t>> ChunkSizes;
			ChunkInfo<LE<uint32_t>> FirstBlockOffsets;
			ChunkInfo<LE<uint16_t>> FirstBlockIndices;
			ChunkInfo<LE<uint16_t>> BlockCount;
			LE<uint16_t> VertexDeclarationCount;
			LE<uint16_t> MaterialCount;
			LE<uint8_t> LodCount;
			LE<uint8_t> EnableIndexBufferStreaming;
			LE<uint8_t> EnableEdgeGeometry;
			LE<uint8_t> Padding;
		};
		static_assert(sizeof ModelBlockLocator == 184);

		struct ModelHeader {
			LE<uint32_t> Version;
			LE<uint32_t> StackSize;
			LE<uint32_t> RuntimeSize;
			LE<uint16_t> VertexDeclarationCount;
			LE<uint16_t> MaterialCount;
			
			LE<uint32_t> VertexOffset[3];
			LE<uint32_t> IndexOffset[3];
			LE<uint32_t> VertexSize[3];
			LE<uint32_t> IndexSize[3];
			
			LE<uint8_t> LodCount;
			LE<uint8_t> EnableIndexBufferStreaming;
			LE<uint8_t> EnableEdgeGeometry;
			LE<uint8_t> Padding;
		};
	}

	extern const uint32_t SqexHashTable[4][256];
	uint32_t SqexHash(const char* data, size_t len);
	uint32_t SqexHash(const std::string& text);
	uint32_t SqexHash(const std::string_view& text);

	template<typename T, typename CountT = T>
	struct AlignResult {
		CountT Count;
		T Alloc;
		T Pad;

		operator T() const {
			return Alloc;
		}
	};

	template<typename T, typename CountT = T>
	AlignResult<T, CountT> Align(T value, T by = static_cast<T>(EntryAlignment)) {
		const auto count = (value + by - 1) / by;
		const auto alloc = count * by;
		const auto pad = alloc - value;
		return {
			.Count = static_cast<CountT>(count),
			.Alloc = static_cast<T>(alloc),
			.Pad = static_cast<T>(pad),
		};
	}

	class FileSystemSqPack {

	public:
		struct SqDataEntry {
			SqIndex::FileSegmentEntry IndexEntry;
			SqIndex::FileSegmentEntry2 Index2Entry;
			uint64_t DataEntryOffset;
			uint32_t DataEntrySize = UINT32_MAX;
			uint32_t DataFileIndex;
		};

		struct SqIndex {
			SqpackHeader Header{};
			SqexDef::SqIndex::Header IndexHeader{};
			std::vector<SqexDef::SqIndex::FolderSegmentEntry> Folders;
			std::map<uint32_t, std::vector<SqexDef::SqIndex::FileSegmentEntry>> Files;
			std::vector<char> DataFileSegment;
			std::vector<SqexDef::SqIndex::Segment3Entry> Segment3;

		private:
			friend class FileSystemSqPack;
			SqIndex(const Utils::Win32::File& hFile, bool strictVerify);
		};

		struct SqIndex2 {
			SqpackHeader Header{};
			SqexDef::SqIndex::Header IndexHeader{};
			std::vector<SqexDef::SqIndex::FolderSegmentEntry> Folders;
			std::vector<SqexDef::SqIndex::FileSegmentEntry2> Files;
			std::vector<char> DataFileSegment;
			std::vector<SqexDef::SqIndex::Segment3Entry> Segment3;

		private:
			friend class FileSystemSqPack;
			SqIndex2(const Utils::Win32::File& hFile, bool strictVerify);
		};

		struct SqData {
			SqpackHeader Header{};
			SqexDef::SqData::Header DataHeader{};
			Utils::Win32::File FileOnDisk;

		private:
			friend class FileSystemSqPack;
			SqData(Utils::Win32::File hFile, uint32_t datIndex, std::vector<SqDataEntry>& dataEntries, bool strictVerify);
		};

		SqIndex Index;
		SqIndex2 Index2;
		std::vector<SqDataEntry> Files;
		std::vector<SqData> Data;

		FileSystemSqPack(const std::filesystem::path& indexFile, bool strictVerify);
	};

	class VirtualSqPack {
		static constexpr uint16_t BlockDataSize = 16000;
		static constexpr uint16_t BlockValidSize = BlockDataSize + sizeof SqData::BlockHeader;
		static constexpr uint16_t BlockPadSize = (EntryAlignment - BlockValidSize) % EntryAlignment;
		static constexpr uint16_t BlockSize = BlockValidSize + BlockPadSize;

		class Implementation;

		class EntryProvider {
		public:
			virtual ~EntryProvider() = default;
			[[nodiscard]] virtual uint32_t Size() const = 0;
			virtual size_t Read(uint64_t offset, void* buf, size_t length) const = 0;
		};

		class EmptyEntryProvider : public EntryProvider {
		public:
			[[nodiscard]] uint32_t Size() const override;
			size_t Read(uint64_t offset, void* buf, size_t length) const override;
		};

		class FileOnDiskEntryProvider : public EntryProvider {
			Utils::Win32::File m_file;
			const uint64_t m_offset;
			const uint32_t m_size;

		public:
			FileOnDiskEntryProvider(Utils::Win32::File file, uint64_t offset, uint32_t length);
			~FileOnDiskEntryProvider() override;

			[[nodiscard]] uint32_t Size() const override;
			size_t Read(uint64_t offset, void* buf, size_t length) const override;
		};

		class OnTheFlyBinaryEntryProvider : public EntryProvider {
			const std::filesystem::path m_path;
			SqData::FileEntryHeader m_header{};

			uint32_t m_padBeforeData = 0;

			mutable Utils::Win32::File m_hFile;

		public:
			OnTheFlyBinaryEntryProvider(std::filesystem::path path);
			~OnTheFlyBinaryEntryProvider() override;

			[[nodiscard]] uint32_t Size() const override;
			size_t Read(uint64_t offset, void* buf, size_t length) const override;
		};

		class MemoryBinaryEntryProvider : public EntryProvider {
			std::vector<char> m_data;

		public:
			MemoryBinaryEntryProvider(const std::filesystem::path& path);
			~MemoryBinaryEntryProvider() override;

			[[nodiscard]] uint32_t Size() const override;
			size_t Read(uint64_t offset, void* buf, size_t length) const override;
		};

		class OnTheFlyModelEntryProvider : public EntryProvider {
			const Utils::Win32::File m_hFile;

			struct ModelEntryHeader {
				SqData::FileEntryHeader Entry;
				SqData::ModelBlockLocator Model;
			};

			ModelEntryHeader m_header{};
			std::vector<uint32_t> m_blockOffsets;
			std::vector<uint16_t> m_blockDataSizes;
			std::vector<uint16_t> m_paddedBlockSizes;
			std::vector<uint32_t> m_actualFileOffsets;

		public:
			OnTheFlyModelEntryProvider(const std::filesystem::path& path);
			~OnTheFlyModelEntryProvider() override;

		private:
			[[nodiscard]] AlignResult<uint32_t> AlignEntry() const;
			[[nodiscard]] uint32_t NextBlockOffset() const;

		public:
			[[nodiscard]] uint32_t Size() const override;
			size_t Read(uint64_t offset, void* buf, size_t length) const override;
		};

		class OnTheFlyTextureEntryProvider : public EntryProvider {
			/*
			 * [MergedHeader]
			 * - [FileEntryHeader]
			 * - [TextureBlockHeaderLocator] * FileEntryHeader.BlockCount
			 * - SubBlockSize: [uint16_t] * TextureBlockHeaderLocator.SubBlockCount * FileEntryHeader.BlockCount
			 * - [TexHeaderBytes]
			 * - - [TexHeader]
			 * - - MipmapOffset: [uint32_t] * TexHeader.MipmapCount
			 * - - [ExtraHeader]
			 * [BlockHeader, Data] * TextureBlockHeaderLocator.SubBlockCount * TexHeader.MipmapCount
			 */
			const Utils::Win32::File m_hFile;

			std::vector<SqData::TextureBlockHeaderLocator> m_blockLocators;
			std::vector<uint16_t> m_subBlockSizes;
			std::vector<uint8_t> m_texHeaderBytes;

			std::vector<uint8_t> m_mergedHeader;

			std::vector<uint32_t> m_mipmapSizes;
			size_t m_size = 0;

			[[nodiscard]] const SqData::TexHeader& AsTexHeader() const;
			[[nodiscard]] std::span<const uint32_t> AsMipmapOffsets() const;

		public:
			OnTheFlyTextureEntryProvider(std::filesystem::path path);
			~OnTheFlyTextureEntryProvider() override;

			[[nodiscard]] uint32_t Size() const override;
			size_t Read(uint64_t offset, void* buf, size_t length) const override;
		};

		struct Entry {
			static constexpr auto NoEntryHash = 0xFFFFFFFF;

			uint32_t PathHash;
			uint32_t NameHash;
			uint32_t FullPathHash;

			uint32_t DataFileIndex;
			uint32_t BlockSize;
			uint32_t PadSize;
			uint64_t OffsetAfterHeaders;

			SqIndex::LEDataLocator Locator;

			std::unique_ptr<EntryProvider> Provider;
		};

		std::vector<std::unique_ptr<Entry>> m_entries;
		std::map<std::pair<uint32_t, uint32_t>, Entry*> m_pathNameTupleEntryPointerMap;
		std::map<uint32_t, Entry*> m_fullPathEntryPointerMap;

		std::vector<SqIndex::FileSegmentEntry> m_fileEntries1;
		std::vector<SqIndex::FileSegmentEntry2> m_fileEntries2;
		std::vector<SqIndex::FolderSegmentEntry> m_folderEntries;

		bool m_frozen = false;

		std::vector<Utils::Win32::File> m_openFiles;

		SqpackHeader m_sqpackIndexHeader{};
		SqIndex::Header m_sqpackIndexSubHeader{};
		std::vector<char> m_sqpackIndexSegment2;
		std::vector<SqIndex::Segment3Entry> m_sqpackIndexSegment3;

		SqpackHeader m_sqpackIndex2Header{};
		SqIndex::Header m_sqpackIndex2SubHeader{};
		std::vector<char> m_sqpackIndex2Segment2;
		std::vector<SqIndex::Segment3Entry> m_sqpackIndex2Segment3;

		SqpackHeader m_sqpackDataHeader{};
		std::vector<SqData::Header> m_sqpackDataSubHeaders;

	public:
		VirtualSqPack();
		~VirtualSqPack() = default;

		struct AddEntryResult {
			size_t AddedCount;
			size_t ReplacedCount;

			AddEntryResult& operator+=(const AddEntryResult& r);
		};

	private:
		AddEntryResult AddEntry(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, std::unique_ptr<EntryProvider> provider);

	public:
		AddEntryResult AddEntriesFromSqPack(const std::filesystem::path& indexPath, bool overwriteUnknownSegments = false);
		AddEntryResult AddEntryFromFile(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, const std::filesystem::path& path);

		[[nodiscard]]
		size_t NumOfDataFiles() const;

	private:
		SqData::Header& AllocateDataSpace(size_t length, bool strict);

	public:
		void Freeze(bool strict);

		size_t ReadIndex1(uint64_t offset, void* buf, size_t length) const;
		size_t ReadIndex2(uint64_t offset, void* buf, size_t length) const;
		size_t ReadData(uint32_t datIndex, uint64_t offset, void* buf, size_t length) const;

		[[nodiscard]] uint64_t SizeIndex1() const;
		[[nodiscard]] uint64_t SizeIndex2() const;
		[[nodiscard]] uint64_t SizeData(uint32_t datIndex) const;
	};

}

#pragma warning(pop)
