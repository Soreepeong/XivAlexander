#pragma once

#include <format>

#include "Sqex.h"

namespace Sqex::Sqpack {
	struct Sha1Value {
		char Value[20]{};

		void Verify(const void* data, size_t size, const char* errorMessage) const;
		template<typename T>
		void Verify(std::span<T> data, const char* errorMessage) const {
			Verify(data.data(), data.size_bytes(), errorMessage);
		}

		void SetFromPointer(const void* data, size_t size);
		template<typename T>
		void SetFrom(std::span<T> data) {
			SetFromPointer(data.data(), data.size_bytes());
		}
		template<typename ...Args>
		void SetFromSpan(Args...args) {
			SetFrom(std::span(std::forward<Args>(args)...));
		}

		bool operator==(const Sha1Value& r) const;
		bool operator!=(const Sha1Value& r) const;
		bool operator==(const char(&r)[20]) const;
		bool operator!=(const char(&r)[20]) const;

		[[nodiscard]] bool IsZero() const;
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
		char Padding_0x024[0x3c0 - 0x024]{};
		Sha1Value Sha1;
		char Padding_0x3D4[0x2c]{};

		void VerifySqpackHeader(SqpackType supposedType);
	};
	static_assert(offsetof(SqpackHeader, Sha1) == 0x3c0, "Bad SqpackHeader definition");
	static_assert(sizeof(SqpackHeader) == 1024);
	
	namespace SqIndex {
		struct SegmentDescriptor {
			LE<uint32_t> Count;
			LE<uint32_t> Offset;
			LE<uint32_t> Size;
			Sha1Value Sha1;
			char Padding_0x020[0x28]{};
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
			char Padding_0x04C[4]{};
			SegmentDescriptor DataFilesSegment;  // Size is always 0x100
			SegmentDescriptor UnknownSegment3;
			SegmentDescriptor FolderSegment;
			char Padding_0x128[4]{};
			LE<IndexType> Type;
			char Padding_0x130[0x3c0 - 0x130]{};
			Sha1Value Sha1;
			char Padding_0x3D4[0x2c]{};

			void VerifySqpackIndexHeader(IndexType expectedIndexType) const;

			void VerifyDataFileSegment(const std::vector<char>& DataFileSegment, int type) const;

		};
		static_assert(sizeof(Header) == 1024);

		struct LEDataLocator : LE<uint32_t> {
			using LE<uint32_t>::LE;
			LEDataLocator(uint32_t index, uint64_t offset);

			[[nodiscard]] uint32_t Index() const { return (Value() & 0xF) / 2; }
			[[nodiscard]] uint64_t Offset() const { return (Value() & 0xFFFFFFF0UL) * 8ULL; }
			uint32_t Index(uint32_t value);
			uint64_t Offset(uint64_t value);
		};

		struct FileSegmentEntry {
			LE<uint32_t> NameHash;
			LE<uint32_t> PathHash;
			LEDataLocator Locator;
			LE<uint32_t> Padding;

			bool operator<(const FileSegmentEntry& r) const {
				if (PathHash == r.PathHash)
					return NameHash < r.NameHash;
				else
					return PathHash < r.PathHash;
			}
		};

		struct FileSegmentEntry2 {
			LE<uint32_t> FullPathHash;
			LEDataLocator Locator;

			bool operator<(const FileSegmentEntry2& r) const {
				return FullPathHash < r.FullPathHash;
			}
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
			static constexpr uint64_t MaxFileSize_MaxValue = 0x800000000ULL;  // 32GiB, maximum addressable via how LEDataLocator works
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
			Sha1Value DataSha1;  // From end of this header to EOF
			char Padding_0x034[0x3c0 - 0x034]{};
			Sha1Value Sha1;
			char Padding_0x3D4[0x2c]{};

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
			
			// See: https://github.com/reusu/FFXIVChnTextPatch/blob/08826ee37acd7461fc7a90f829ac1faee1f68933/src/main/java/name/yumao/ffxiv/chn/builder/BinaryBlockBuilder.java#L78
			// Unknown1 does contain a value in original sqpack files, but the game works nonetheless when 0 is given.
			LE<uint32_t> Unknown1;
			// (sizeof (aligned size of this entry) - sizeof FileEntryHeader) / 128
			LE<uint32_t> AlignedUnitAllocationCount;

			LE<uint32_t> BlockCountOrVersion;
		};

		struct TextureBlockHeaderLocator {
			LE<uint32_t> FirstBlockOffset;
			LE<uint32_t> TotalSize;
			LE<uint32_t> DecompressedSize;
			LE<uint32_t> FirstSubBlockIndex;
			LE<uint32_t> SubBlockCount;
		};
		
		struct ModelBlockLocator {
			static const size_t EntryIndexMap[11];

			template<typename T>
			struct ChunkInfo {
				T Stack;
				T Runtime;
				T Vertex[3];
				T EdgeGeometryVertex[3];
				T Index[3];

				[[nodiscard]] const T& EntryAt(size_t i) const {
					return (&Stack)[EntryIndexMap[i]];
				}
				T& EntryAt(size_t i) {
					return (&Stack)[EntryIndexMap[i]];
				}
			};

			ChunkInfo<LE<uint32_t>> AlignedDecompressedSizes;
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
	}

	extern const uint32_t SqexHashTable[4][256];
	uint32_t SqexHash(const char* data, size_t len);
	uint32_t SqexHash(const std::string& text);
	uint32_t SqexHash(const std::string_view& text);
	uint32_t SqexHash(const std::filesystem::path& path);

	struct EntryPathSpec {
		static constexpr auto EmptyHashValue = 0xFFFFFFFF;

		std::filesystem::path Original;

		uint32_t PathHash;
		uint32_t NameHash;
		uint32_t FullPathHash;

		EntryPathSpec()
			: PathHash(EmptyHashValue)
			, NameHash(EmptyHashValue)
			, FullPathHash(EmptyHashValue) {
		}

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash)
			: PathHash(pathHash)
			, NameHash(nameHash)
			, FullPathHash(fullPathHash) {
		}

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash)
			: PathHash(pathHash)
			, NameHash(nameHash)
			, FullPathHash(EmptyHashValue) {
		}

		EntryPathSpec(uint32_t fullPathHash)
			: PathHash(EmptyHashValue)
			, NameHash(EmptyHashValue)
			, FullPathHash(fullPathHash) {
		}

		EntryPathSpec(const std::filesystem::path& fullPath)
			: Original(fullPath.lexically_normal())
			, PathHash(SqexHash(Original.parent_path()))
			, NameHash(SqexHash(Original.filename()))
			, FullPathHash(SqexHash(Original)) {
		}

		template<class Elem, class Traits = std::char_traits<Elem>, class Alloc = std::allocator<Elem>>
		EntryPathSpec(const std::basic_string<Elem, Traits, Alloc>& fullPath)
			: Original(std::filesystem::path(fullPath).lexically_normal())
			, PathHash(SqexHash(Original.parent_path()))
			, NameHash(SqexHash(Original.filename()))
			, FullPathHash(SqexHash(Original)) {
		}

		EntryPathSpec(const char* fullPath)
			: Original(std::filesystem::path(fullPath).lexically_normal())
			, PathHash(SqexHash(Original.parent_path()))
			, NameHash(SqexHash(Original.filename()))
			, FullPathHash(SqexHash(Original)) {
		}

		EntryPathSpec(const std::filesystem::path& path, const std::filesystem::path& name)
			: Original((path / name).lexically_normal())
			, PathHash(SqexHash(path))
			, NameHash(SqexHash(name))
			, FullPathHash(SqexHash(Original)) {
		}

		EntryPathSpec& operator=(const std::filesystem::path& fullPath) {
			Original = fullPath.lexically_normal();
			PathHash = SqexHash(Original.parent_path());
			NameHash = SqexHash(Original.filename());
			FullPathHash = SqexHash(Original);
			return *this;
		}

		template<class Elem, class Traits = std::char_traits<Elem>, class Alloc = std::allocator<Elem>>
		EntryPathSpec& operator=(const std::basic_string<Elem, Traits, Alloc>& fullPath) {
			Original = std::filesystem::path(fullPath).lexically_normal();
			PathHash = SqexHash(Original.parent_path());
			NameHash = SqexHash(Original.filename());
			FullPathHash = SqexHash(Original);
			return *this;
		}

		EntryPathSpec& operator=(uint32_t fullPathHash) {
			Original.clear();
			PathHash = EmptyHashValue;
			NameHash = EmptyHashValue;
			FullPathHash = fullPathHash;
			return *this;
		}

		[[nodiscard]] bool HasFullPathHash() const {
			return FullPathHash != EmptyHashValue;
		}

		[[nodiscard]] bool HasComponentHash() const {
			return PathHash != EmptyHashValue || NameHash != EmptyHashValue;
		}

		[[nodiscard]] bool empty() const {
			return PathHash == EmptyHashValue && NameHash == EmptyHashValue && FullPathHash == EmptyHashValue;
		}

		bool operator==(const EntryPathSpec& r) const {
			return (HasComponentHash() && PathHash == r.PathHash && NameHash == r.NameHash)
				|| (HasFullPathHash() && FullPathHash == r.FullPathHash)
				|| (empty() && r.empty());
		}

		bool operator!=(const EntryPathSpec& r) const {
			return !this->operator==(r);
		}

		bool operator<(const EntryPathSpec& r) const {
			if (FullPathHash != r.FullPathHash)
				return FullPathHash < r.FullPathHash;
			if (PathHash != r.PathHash)
				return PathHash < r.PathHash;
			if (NameHash != r.NameHash)
				return NameHash < r.NameHash;
			return false;
		}

		bool operator>(const EntryPathSpec& r) const {
			if (FullPathHash != r.FullPathHash)
				return FullPathHash > r.FullPathHash;
			if (PathHash != r.PathHash)
				return PathHash > r.PathHash;
			if (NameHash != r.NameHash)
				return NameHash > r.NameHash;
			return false;
		}
	};

}

template<>
struct std::formatter<Sqex::Sqpack::EntryPathSpec, char> : std::formatter<std::basic_string<char>, char> {
	template<class FormatContext>
	auto format(const Sqex::Sqpack::EntryPathSpec& t, FormatContext& fc) {
		return std::formatter<std::basic_string<char>, char>::format(std::format("{}({:08x}/{:08x}, {:08x})", t.Original.wstring(), t.PathHash, t.NameHash, t.FullPathHash), fc);
	}
};
