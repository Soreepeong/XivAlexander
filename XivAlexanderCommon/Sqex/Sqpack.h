#pragma once

#include <format>

#include "XivAlexanderCommon/Sqex.h"

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
		LE<uint32_t> Unknown2;  // Intl: 0xFFFFFFFF, KR/CN: 1
		char Padding_0x024[0x3c0 - 0x024]{};
		Sha1Value Sha1;
		char Padding_0x3D4[0x2c]{};

		void VerifySqpackHeader(SqpackType supposedType) const;
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
		 * * Descriptor.Size is multiple of 0x100; each entry is sized 0x100
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
			SegmentDescriptor HashLocatorSegment;
			char Padding_0x04C[4]{};
			SegmentDescriptor TextLocatorSegment;
			SegmentDescriptor UnknownSegment3;
			SegmentDescriptor PathHashLocatorSegment;
			char Padding_0x128[4]{};
			LE<IndexType> Type;
			char Padding_0x130[0x3c0 - 0x130]{};
			Sha1Value Sha1;
			char Padding_0x3D4[0x2c]{};

			void VerifySqpackIndexHeader(IndexType expectedIndexType) const;
		};
		static_assert(sizeof(Header) == 1024);

		union LEDataLocator {
			static LEDataLocator Synonym() {
				return {1};
			}

			uint32_t Value;
			struct {
				uint32_t IsSynonym : 1;
				uint32_t DatFileIndex : 3;
				uint32_t DatFileOffsetBy8 : 28;
			};

			LEDataLocator(const LEDataLocator& r)
				: IsSynonym(r.IsSynonym)
				, DatFileIndex(r.DatFileIndex)
				, DatFileOffsetBy8(r.DatFileOffsetBy8) {
			}

			LEDataLocator(uint32_t value = 0)
				: Value(value) {
			}

			LEDataLocator(uint32_t index, uint64_t offset)
				: IsSynonym(0)
				, DatFileIndex(index)
				, DatFileOffsetBy8(static_cast<uint32_t>(offset / EntryAlignment)) {
				if (offset % EntryAlignment)
					throw std::invalid_argument("Offset must be a multiple of 128.");
				if (offset / 8 > UINT32_MAX)
					throw std::invalid_argument("Offset is too big.");
			}

			[[nodiscard]] uint64_t DatFileOffset() const {
				return 1ULL * DatFileOffsetBy8 * EntryAlignment;
			}

			uint64_t DatFileOffset(uint64_t value) {
				if (value % EntryAlignment)
					throw std::invalid_argument("Offset must be a multiple of 128.");
				if (value / 8 > UINT32_MAX)
					throw std::invalid_argument("Offset is too big.");
				DatFileOffsetBy8 = static_cast<uint32_t>(value / EntryAlignment);
			}

			bool operator<(const LEDataLocator& r) const {
				return Value < r.Value;
			}

			bool operator>(const LEDataLocator& r) const {
				return Value > r.Value;
			}

			bool operator==(const LEDataLocator& r) const {
				if (IsSynonym || r.IsSynonym)
					return IsSynonym == r.IsSynonym;
				return Value == r.Value;
			}
		};

		struct PairHashLocator {
			LE<uint32_t> NameHash;
			LE<uint32_t> PathHash;
			LEDataLocator Locator;
			LE<uint32_t> Padding;

			bool operator<(const PairHashLocator& r) const {
				if (PathHash == r.PathHash)
					return NameHash < r.NameHash;
				else
					return PathHash < r.PathHash;
			}
		};

		struct FullHashLocator {
			LE<uint32_t> FullPathHash;
			LEDataLocator Locator;

			bool operator<(const FullHashLocator& r) const {
				return FullPathHash < r.FullPathHash;
			}
		};

		struct Segment3Entry {
			LE<uint32_t> Unknown1;
			LE<uint32_t> Unknown2;
			LE<uint32_t> Unknown3;
			LE<uint32_t> Unknown4;
		};

		struct PathHashLocator {
			LE<uint32_t> PathHash;
			LE<uint32_t> PairHashLocatorOffset;
			LE<uint32_t> PairHashLocatorSize;
			LE<uint32_t> Padding;

			void Verify() const;
		};

		struct PairHashWithTextLocator {
			static constexpr uint32_t EndOfList = 0xFFFFFFFFU;

			// TODO: following two can actually be in reverse order; find it out when the game data file actually contains a conflict in .index file
			Utils::LE<uint32_t> NameHash;
			Utils::LE<uint32_t> PathHash;
			Sqex::Sqpack::SqIndex::LEDataLocator Locator;
			Utils::LE<uint32_t> ConflictIndex;
			char FullPath[0xF0];
		};

		struct FullHashWithTextLocator {
			static constexpr uint32_t EndOfList = 0xFFFFFFFFU;

			Utils::LE<uint32_t> FullPathHash;
			Utils::LE<uint32_t> UnusedHash;
			Sqex::Sqpack::SqIndex::LEDataLocator Locator;
			Utils::LE<uint32_t> ConflictIndex;
			char FullPath[0xF0];
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
					if (value % EntryAlignment)
						throw std::invalid_argument("Value must be a multiple of 8.");
					if (value / EntryAlignment > UINT32_MAX)
						throw std::invalid_argument("Value too big.");
					RawValue = static_cast<uint32_t>(value / EntryAlignment);
					return *this;
				}

				operator uint64_t() const {
					return Value();
				}

				[[nodiscard]] uint64_t Value() const {
					return 1ULL * RawValue * EntryAlignment;
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
			None = 0,
			EmptyOrObfuscated = 1,
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

			LE<uint32_t> AllocatedSpaceUnitCount; // (Allocation - HeaderSize) / OffsetUnit
			LE<uint32_t> OccupiedSpaceUnitCount;

			LE<uint32_t> BlockCountOrVersion;

			static FileEntryHeader NewEmpty(uint64_t decompressedSize = 0, uint64_t compressedSize = 0);

			void SetSpaceUnits(uint64_t dataSize);
			[[nodiscard]] uint64_t GetDataSize() const;

			[[nodiscard]] uint64_t GetTotalEntrySize() const;
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

	static constexpr uint16_t EntryBlockDataSize = 16000;
	static constexpr uint16_t EntryBlockValidSize = EntryBlockDataSize + sizeof SqData::BlockHeader;
	static constexpr uint16_t EntryBlockPadSize = (EntryAlignment - EntryBlockValidSize) % EntryAlignment;
	static constexpr uint16_t EntryBlockSize = EntryBlockValidSize + EntryBlockPadSize;

	extern const uint32_t SqexHashTable[4][256];
	uint32_t SqexHash(const char* data, size_t len = SIZE_MAX);
	uint32_t SqexHash(const std::string& text);
	uint32_t SqexHash(const std::string_view& text);
	uint32_t SqexHash(const std::filesystem::path& path);

	struct EntryPathSpec {
		static constexpr auto EmptyHashValue = 0xFFFFFFFFU;

		std::filesystem::path FullPath;

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

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash, const std::string& fullPath)
			: FullPath(std::filesystem::path(Utils::FromUtf8(fullPath)).lexically_normal())
			, PathHash(pathHash)
			, NameHash(nameHash)
			, FullPathHash(fullPathHash) {
		}

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash, const std::string& fullPath)
			: FullPath(std::filesystem::path(Utils::FromUtf8(fullPath)).lexically_normal())
			, PathHash(pathHash)
			, NameHash(nameHash)
			, FullPathHash(EmptyHashValue) {
		}

		EntryPathSpec(uint32_t fullPathHash, const std::string& fullPath)
			: FullPath(std::filesystem::path(Utils::FromUtf8(fullPath)).lexically_normal())
			, PathHash(EmptyHashValue)
			, NameHash(EmptyHashValue)
			, FullPathHash(fullPathHash) {
		}

		EntryPathSpec(const std::filesystem::path& fullPath)
			: FullPath(fullPath.lexically_normal())
			, PathHash(SqexHash(FullPath.parent_path()))
			, NameHash(SqexHash(FullPath.filename()))
			, FullPathHash(SqexHash(FullPath)) {
		}

		EntryPathSpec(const std::string& fullPath)
			: FullPath(std::filesystem::path(Utils::FromUtf8(fullPath)).lexically_normal())
			, PathHash(SqexHash(FullPath.parent_path()))
			, NameHash(SqexHash(FullPath.filename()))
			, FullPathHash(SqexHash(FullPath)) {
		}

		EntryPathSpec(const std::wstring& fullPath)
			: FullPath(std::filesystem::path(fullPath).lexically_normal())
			, PathHash(SqexHash(FullPath.parent_path()))
			, NameHash(SqexHash(FullPath.filename()))
			, FullPathHash(SqexHash(FullPath)) {
		}

		EntryPathSpec(const char* fullPath)
			: FullPath(std::filesystem::path(Utils::FromUtf8(fullPath)).lexically_normal())
			, PathHash(SqexHash(FullPath.parent_path()))
			, NameHash(SqexHash(FullPath.filename()))
			, FullPathHash(SqexHash(FullPath)) {
		}

		EntryPathSpec(const wchar_t* fullPath)
			: FullPath(std::filesystem::path(fullPath).lexically_normal())
			, PathHash(SqexHash(FullPath.parent_path()))
			, NameHash(SqexHash(FullPath.filename()))
			, FullPathHash(SqexHash(FullPath)) {
		}

		EntryPathSpec(const std::filesystem::path& path, const std::filesystem::path& name)
			: FullPath((path / name).lexically_normal())
			, PathHash(SqexHash(path))
			, NameHash(SqexHash(name))
			, FullPathHash(SqexHash(FullPath)) {
		}

		EntryPathSpec& operator=(const std::filesystem::path& fullPath) {
			FullPath = fullPath.lexically_normal();
			PathHash = SqexHash(FullPath.parent_path());
			NameHash = SqexHash(FullPath.filename());
			FullPathHash = SqexHash(FullPath);
			return *this;
		}

		template<class Elem, class Traits = std::char_traits<Elem>, class Alloc = std::allocator<Elem>>
		EntryPathSpec& operator=(const std::basic_string<Elem, Traits, Alloc>& fullPath) {
			FullPath = std::filesystem::path(fullPath).lexically_normal();
			PathHash = SqexHash(FullPath.parent_path());
			NameHash = SqexHash(FullPath.filename());
			FullPathHash = SqexHash(FullPath);
			return *this;
		}

		EntryPathSpec& operator=(uint32_t fullPathHash) {
			FullPath.clear();
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

		[[nodiscard]] bool HasOriginal() const {
			return !FullPath.empty();
		}

		[[nodiscard]] bool empty() const {
			return PathHash == EmptyHashValue && NameHash == EmptyHashValue && FullPathHash == EmptyHashValue;
		}

		[[nodiscard]] std::string NativeRepresentation() const {
			auto s = Utils::ToUtf8(FullPath.c_str());
			for (auto& c : s)
				if (c == '\\')
					c = '/';
			return s;
		}

		[[nodiscard]] std::string DatFile() const;
		[[nodiscard]] std::string DatExpac() const;

		bool operator==(const EntryPathSpec& r) const {
			if (HasOriginal() && r.HasOriginal())
				return lstrcmpiW(FullPath.c_str(), r.FullPath.c_str()) == 0;

			return (HasComponentHash() && PathHash == r.PathHash && NameHash == r.NameHash)
				|| (HasFullPathHash() && FullPathHash == r.FullPathHash)
				|| (empty() && r.empty());
		}

		bool operator!=(const EntryPathSpec& r) const {
			return !this->operator==(r);
		}

		struct FullComparator {
			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				if (l.HasOriginal() && r.HasOriginal()) {
					const auto cmp = lstrcmpiW(l.FullPath.c_str(), r.FullPath.c_str());
					if (cmp != 0)
						return cmp < 0;
				}

				if (l.FullPathHash != r.FullPathHash)
					return l.FullPathHash < r.FullPathHash;
				if (l.PathHash != r.PathHash)
					return l.PathHash < r.PathHash;
				if (l.NameHash != r.NameHash)
					return l.NameHash < r.NameHash;
				return false;
			}
		};

		struct AllHashComparator {
			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				if (l.FullPathHash != r.FullPathHash)
					return l.FullPathHash < r.FullPathHash;
				if (l.PathHash != r.PathHash)
					return l.PathHash < r.PathHash;
				if (l.NameHash != r.NameHash)
					return l.NameHash < r.NameHash;
				return false;
			}
		};

		struct FullHashComparator {
			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				return l.FullPathHash < r.FullPathHash;
			}
		};

		struct PairHashComparator {
			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				if (l.PathHash == r.PathHash)
					return l.NameHash < r.NameHash;
				else
					return l.PathHash < r.PathHash;
			}
		};

		struct FullPathComparator {
			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				const auto cmp = lstrcmpiW(l.FullPath.c_str(), r.FullPath.c_str());
				return cmp < 0;
			}
		};
	};

	struct PathSpecComparator {
		bool operator()(const SqIndex::PairHashLocator& l, uint32_t r) const {
			return l.NameHash < r;
		}

		bool operator()(uint32_t l, const SqIndex::PairHashLocator& r) const {
			return l < r.NameHash;
		}

		bool operator()(const SqIndex::PathHashLocator& l, uint32_t r) const {
			return l.PathHash < r;
		}

		bool operator()(uint32_t l, const SqIndex::PathHashLocator& r) const {
			return l < r.PathHash;
		}

		bool operator()(const SqIndex::FullHashLocator& l, uint32_t r) const {
			return l.FullPathHash < r;
		}

		bool operator()(uint32_t l, const SqIndex::FullHashLocator& r) const {
			return l < r.FullPathHash;
		}

		bool operator()(const SqIndex::PairHashWithTextLocator& l, const char* rt) const {
			return _strcmpi(l.FullPath, rt);
		}

		bool operator()(const char* lt, const SqIndex::PairHashWithTextLocator& r) const {
			return _strcmpi(lt, r.FullPath);
		}

		bool operator()(const SqIndex::FullHashWithTextLocator& l, const char* rt) const {
			return _strcmpi(l.FullPath, rt);
		}

		bool operator()(const char* lt, const SqIndex::FullHashWithTextLocator& r) const {
			return _strcmpi(lt, r.FullPath);
		}
	};
}

template<>
struct std::formatter<Sqex::Sqpack::EntryPathSpec, char> : std::formatter<std::basic_string<char>, char> {
	template<class FormatContext>
	auto format(const Sqex::Sqpack::EntryPathSpec& t, FormatContext& fc) const {
		return std::formatter<std::basic_string<char>, char>::format(std::format("{}({:08x}/{:08x}, {:08x})", t.FullPath.wstring(), t.PathHash, t.NameHash, t.FullPathHash), fc);
	}
};
