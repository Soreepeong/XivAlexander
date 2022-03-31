#pragma once

#include <format>
#include <numeric>

#include <zlib.h>

#include "../Sqex.h"

#include "internal/sha1.h"

namespace Sqex::Sqpack {
	struct Sha1Value {
		uint8_t Value[20]{};

		bool operator==(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) == 0;
		}

		bool operator!=(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) != 0;
		}

		bool operator<(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) < 0;
		}

		bool operator<=(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) <= 0;
		}

		bool operator>(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) > 0;
		}

		bool operator>=(const Sha1Value& r) const {
			return memcmp(r.Value, Value, sizeof Value) >= 0;
		}

		bool operator==(const char(&r)[20]) const {
			return memcmp(r, Value, sizeof Value) == 0;
		}

		bool operator!=(const char(&r)[20]) const {
			return memcmp(r, Value, sizeof Value) != 0;
		}

		[[nodiscard]] bool IsZero() const {
			return IsAllSameValue(Value);
		}

		void SetFromPointer(const void* data, size_t size) {
			Internal::SHA1 hasher;
			hasher.processBytes(data, size);
			hasher.getDigestBytes(Value);
		}

		template<typename T>
		void SetFrom(std::span<T> data) {
			SetFromPointer(data.data(), data.size_bytes());
		}

		template<typename ...Args>
		void SetFromSpan(Args...args) {
			SetFrom(std::span(std::forward<Args>(args)...));
		}

		void Verify(const void* data, size_t size, const char* errorMessage) const {
			Sha1Value t;
			t.SetFromPointer(data, size);
			if (*this != t) {
				if (!size && IsAllSameValue(Value))  // all zero values can be in place of SHA-1 value of empty value
					return;
				throw CorruptDataException(errorMessage);
			}
		}

		template<typename T>
		void Verify(std::span<T> data, const char* errorMessage) const {
			Verify(data.data(), data.size_bytes(), errorMessage);
		}
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
		static constexpr char Signature_Value[12] = {
			'S', 'q', 'P', 'a', 'c', 'k', 0, 0, 0, 0, 0, 0,
		};

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

		void VerifySqpackHeader(SqpackType supposedType) const {
			if (HeaderSize != sizeof SqpackHeader)
				throw CorruptDataException("sizeof Header != 0x400");
			if (memcmp(Signature, Signature_Value, sizeof Signature) != 0)
				throw CorruptDataException("Invalid SqPack signature");
			Sha1.Verify(this, offsetof(Sqex::Sqpack::SqpackHeader, Sha1), "SqPack Header SHA-1");
			if (!IsAllSameValue(Padding_0x024))
				throw CorruptDataException("Padding_0x024 != 0");
			if (!IsAllSameValue(Padding_0x3D4))
				throw CorruptDataException("Padding_0x3D4 != 0");
			if (supposedType != SqpackType::Unspecified && supposedType != Type)
				throw CorruptDataException(std::format("Invalid SqpackType (expected {}, file is {})",
					static_cast<uint32_t>(supposedType),
					static_cast<uint32_t>(*Type)));
		}
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

			void VerifySqpackIndexHeader(IndexType expectedIndexType) const {
				if (HeaderSize != sizeof Header)
					throw CorruptDataException("sizeof IndexHeader != 0x400");
				if (expectedIndexType != IndexType::Unspecified && expectedIndexType != Type)
					throw CorruptDataException(std::format("Invalid SqpackType (expected {}, file is {})",
						static_cast<uint32_t>(expectedIndexType),
						static_cast<uint32_t>(*Type)));
				Sha1.Verify(this, offsetof(Sqex::Sqpack::SqIndex::Header, Sha1), "SqIndex Header SHA-1");
				if (!IsAllSameValue(Padding_0x04C))
					throw CorruptDataException("Padding_0x04C");
				if (!IsAllSameValue(Padding_0x128))
					throw CorruptDataException("Padding_0x128");
				if (!IsAllSameValue(Padding_0x130))
					throw CorruptDataException("Padding_0x130");
				if (!IsAllSameValue(Padding_0x3D4))
					throw CorruptDataException("Padding_0x3D4");

				if (!IsAllSameValue(HashLocatorSegment.Padding_0x020))
					throw CorruptDataException("HashLocatorSegment.Padding_0x020");
				if (!IsAllSameValue(TextLocatorSegment.Padding_0x020))
					throw CorruptDataException("TextLocatorSegment.Padding_0x020");
				if (!IsAllSameValue(UnknownSegment3.Padding_0x020))
					throw CorruptDataException("UnknownSegment3.Padding_0x020");
				if (!IsAllSameValue(PathHashLocatorSegment.Padding_0x020))
					throw CorruptDataException("PathHashLocatorSegment.Padding_0x020");

				if (Type == IndexType::Index && HashLocatorSegment.Size % sizeof PairHashLocator)
					throw CorruptDataException("HashLocatorSegment.size % sizeof FileSegmentEntry != 0");
				else if (Type == IndexType::Index2 && HashLocatorSegment.Size % sizeof FullHashLocator)
					throw CorruptDataException("HashLocatorSegment.size % sizeof FileSegmentEntry2 != 0");
				if (UnknownSegment3.Size % sizeof Segment3Entry)
					throw CorruptDataException("UnknownSegment3.size % sizeof Segment3Entry != 0");
				if (PathHashLocatorSegment.Size % sizeof PathHashLocator)
					throw CorruptDataException("PathHashLocatorSegment.size % sizeof FolderSegmentEntry != 0");

				if (HashLocatorSegment.Count != 1)
					throw CorruptDataException("Segment1.Count == 1");
				if (UnknownSegment3.Count != 0)
					throw CorruptDataException("Segment3.Count == 0");
				if (PathHashLocatorSegment.Count != 0)
					throw CorruptDataException("Segment4.Count == 0");
			}
		};
		static_assert(sizeof(Header) == 1024);

		union LEDataLocator {
			static LEDataLocator Synonym() {
				return { 1 };
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

			void Verify() const {
				if (PairHashLocatorSize % sizeof(PairHashLocator))
					throw CorruptDataException("FolderSegmentEntry.FileSegmentSize % sizeof FileSegmentEntry != 0");
			}

		};

		struct PairHashWithTextLocator {
			static constexpr uint32_t EndOfList = 0xFFFFFFFFU;

			// TODO: following two can actually be in reverse order; find it out when the game data file actually contains a conflict in .index file
			LE<uint32_t> NameHash;
			LE<uint32_t> PathHash;
			Sqex::Sqpack::SqIndex::LEDataLocator Locator;
			LE<uint32_t> ConflictIndex;
			char FullPath[0xF0];
		};

		struct FullHashWithTextLocator {
			static constexpr uint32_t EndOfList = 0xFFFFFFFFU;

			LE<uint32_t> FullPathHash;
			LE<uint32_t> UnusedHash;
			Sqex::Sqpack::SqIndex::LEDataLocator Locator;
			LE<uint32_t> ConflictIndex;
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

			void Verify(uint32_t expectedSpanIndex) const {
				if (HeaderSize != sizeof Header)
					throw CorruptDataException("sizeof IndexHeader != 0x400");
				Sha1.Verify(span_cast<char>(1, this).subspan(0, offsetof(Sqex::Sqpack::SqData::Header, Sha1)), "IndexHeader SHA-1");
				if (*Null1)
					throw CorruptDataException("Null1 != 0");
				if (Unknown1 != Unknown1_Value)
					throw CorruptDataException(std::format("Unknown1({:x}) != Unknown1_Value({:x})", *Unknown1, Unknown1_Value));
				//if (SpanIndex != expectedSpanIndex)
				//	throw CorruptDataException(std::format("SpanIndex({}) != ExpectedSpanIndex({})", *SpanIndex, expectedSpanIndex));
				if (*Null2)
					throw CorruptDataException("Null2 != 0");
				if (MaxFileSize > MaxFileSize_MaxValue)
					throw CorruptDataException(std::format("MaxFileSize({:x}) != MaxFileSize_MaxValue({:x})", *MaxFileSize, MaxFileSize_MaxValue));
				if (!IsAllSameValue(Padding_0x034))
					throw CorruptDataException("Padding_0x034 != 0");
				if (!IsAllSameValue(Padding_0x3D4))
					throw CorruptDataException("Padding_0x3D4 != 0");
			}
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

			static FileEntryHeader NewEmpty(uint64_t decompressedSize = 0, uint64_t compressedSize = 0) {
				FileEntryHeader res{
					.HeaderSize = static_cast<uint32_t>(Align(sizeof FileEntryHeader)),
					.Type = FileEntryType::EmptyOrObfuscated,
					.DecompressedSize = static_cast<uint32_t>(decompressedSize),
					.BlockCountOrVersion = static_cast<uint32_t>(compressedSize),
				};
				res.SetSpaceUnits(compressedSize);
				return res;
			}

			void SetSpaceUnits(uint64_t dataSize) {
				AllocatedSpaceUnitCount = OccupiedSpaceUnitCount = Align<uint64_t, uint32_t>(dataSize, EntryAlignment).Count;
			}

			[[nodiscard]] uint64_t GetDataSize() const {
				return 1ULL * OccupiedSpaceUnitCount * EntryAlignment;
			}

			[[nodiscard]] uint64_t GetTotalEntrySize() const {
				return HeaderSize + GetDataSize();
			}
		};

		struct TextureBlockHeaderLocator {
			LE<uint32_t> FirstBlockOffset;
			LE<uint32_t> TotalSize;
			LE<uint32_t> DecompressedSize;
			LE<uint32_t> FirstSubBlockIndex;
			LE<uint32_t> SubBlockCount;
		};

		struct ModelBlockLocator {
			static constexpr size_t EntryIndexMap[11] = {
				0, 1, 2, 5, 8, 3, 6, 9, 4, 7, 10,
			};

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

	struct EntryPathSpec {
		static constexpr uint32_t EmptyHashValue = 0xFFFFFFFFU;
		static constexpr uint8_t EmptyId = 0xFF;
		static constexpr uint32_t SlashHashValue = 0x862C2D2BU;

	private:
		bool m_empty;
		uint8_t m_categoryId;
		uint8_t m_expacId;
		uint8_t m_partId;
		uint32_t m_pathHash;
		uint32_t m_nameHash;
		uint32_t m_fullPathHash;
		std::string m_text;

	public:
		EntryPathSpec()
			: m_empty(true)
			, m_categoryId(EmptyId)
			, m_expacId(EmptyId)
			, m_partId(EmptyId)
			, m_pathHash(EmptyHashValue)
			, m_nameHash(EmptyHashValue)
			, m_fullPathHash(EmptyHashValue)
			, m_text() {
		}

		EntryPathSpec(EntryPathSpec&& r)
			: m_empty(r.m_empty)
			, m_categoryId(r.m_categoryId)
			, m_expacId(r.m_expacId)
			, m_partId(r.m_partId)
			, m_fullPathHash(r.m_fullPathHash)
			, m_pathHash(r.m_pathHash)
			, m_nameHash(r.m_nameHash)
			, m_text(std::move(r.m_text)) {
			r.clear();
		}

		EntryPathSpec(const EntryPathSpec& r)
			: m_empty(r.m_empty)
			, m_categoryId(r.m_categoryId)
			, m_expacId(r.m_expacId)
			, m_partId(r.m_partId)
			, m_fullPathHash(r.m_fullPathHash)
			, m_pathHash(r.m_pathHash)
			, m_nameHash(r.m_nameHash)
			, m_text(r.m_text) {
		}

		EntryPathSpec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash, uint8_t categoryId, uint8_t expacId, uint8_t partId)
			: m_empty(false)
			, m_categoryId(categoryId)
			, m_expacId(expacId)
			, m_partId(partId)
			, m_pathHash(pathHash)
			, m_nameHash(nameHash)
			, m_fullPathHash(fullPathHash) {
		}

		EntryPathSpec(std::string fullPath) : EntryPathSpec() {
			auto pos = fullPath.find('/');
			std::vector<std::string_view> parts;
			size_t previousOffset = 0, offset;
			while ((offset = fullPath.find_first_of("/\\", previousOffset)) != std::string::npos) {
				auto part = std::string_view(fullPath).substr(previousOffset, offset - previousOffset);
				previousOffset = offset + 1;

				if (part.empty() || part == ".")
					void();
				else if (part == "..") {
					if (!parts.empty())
						parts.pop_back();
				} else {
					parts.push_back(part);
				}
			}

			if (auto part = std::string_view(fullPath).substr(previousOffset); part.empty() || part == ".")
				void();
			else if (part == "..") {
				if (!parts.empty())
					parts.pop_back();
			} else {
				parts.push_back(part);
			}

			if (parts.empty())
				return;

			m_empty = false;
			m_text.reserve(std::accumulate(parts.begin(), parts.end(), SIZE_MAX, [](size_t curr, const std::wstring_view& view) { return curr + view.size() + 1; }));

			m_pathHash = m_nameHash = 0;
			for (size_t i = 0; i < parts.size(); i++) {
				if (i > 0) {
					m_text += "/";
					m_pathHash = crc32_combine(crc32_combine(m_pathHash, ~SlashHashValue, 1), m_nameHash, static_cast<long>(parts[i - 1].size()));
				}
				m_text += parts[i];
				m_nameHash = crc32_z(0, reinterpret_cast<const uint8_t*>(parts[i].data()), parts[i].size());
			}

			m_fullPathHash = crc32_combine(crc32_combine(m_pathHash, ~SlashHashValue, 1), m_nameHash, parts.empty() ? 0 : static_cast<long>(parts.back().size()));

			m_fullPathHash = ~m_fullPathHash;
			m_pathHash = ~m_pathHash;
			m_nameHash = ~m_nameHash;

			if (!parts.empty()) {
				m_expacId = m_partId = 0;

				if (parts[0] == "common") {
					m_categoryId = 0x00;

				} else if (parts[0] == "bgcommon") {
					m_categoryId = 0x01;

				} else if (parts[0] == "bg") {
					m_categoryId = 0x02;
					m_expacId = parts.size() >= 2 && parts[1].starts_with("ex") ? std::strtol(&parts[1][2], nullptr, 10) : 0;
					m_partId = parts.size() >= 3 && m_expacId > 0 ? std::strtol(&parts[2][0], nullptr, 10) : 0;

				} else if (parts[0] == "cut") {
					m_categoryId = 0x03;
					m_expacId = parts.size() >= 2 && parts[1].starts_with("ex") ? std::strtol(&parts[1][2], nullptr, 10) : 0;

				} else if (parts[0] == "chara") {
					m_categoryId = 0x04;

				} else if (parts[0] == "shader") {
					m_categoryId = 0x05;

				} else if (parts[0] == "ui") {
					m_categoryId = 0x06;

				} else if (parts[0] == "sound") {
					m_categoryId = 0x07;

				} else if (parts[0] == "vfx") {
					m_categoryId = 0x08;

				} else if (parts[0] == "exd") {
					m_categoryId = 0x0a;

				} else if (parts[0] == "game_script") {
					m_categoryId = 0x0b;

				} else if (parts[0] == "music") {
					m_categoryId = 0x0c;
					m_expacId = parts.size() >= 2 && parts[1].starts_with("ex") ? std::strtol(&parts[1][2], nullptr, 10) : 0;

				} else
					m_categoryId = 0x00;
			}
		}

		EntryPathSpec& operator=(EntryPathSpec&& r) {
			m_empty = r.m_empty;
			m_categoryId = r.m_categoryId;
			m_expacId = r.m_expacId;
			m_partId = r.m_partId;
			m_fullPathHash = r.m_fullPathHash;
			m_pathHash = r.m_pathHash;
			m_nameHash = r.m_nameHash;
			m_text = std::move(r.m_text);
			r.clear();
			return *this;
		}

		EntryPathSpec& operator=(const EntryPathSpec& r) {
			m_empty = r.m_empty;
			m_categoryId = r.m_categoryId;
			m_expacId = r.m_expacId;
			m_partId = r.m_partId;
			m_fullPathHash = r.m_fullPathHash;
			m_pathHash = r.m_pathHash;
			m_nameHash = r.m_nameHash;
			m_text = r.m_text;
			return *this;
		}

		[[nodiscard]] bool HasOriginal() const { return !m_text.empty(); }

		[[nodiscard]] bool Empty() const { return m_empty; }

		uint8_t CategoryId() const { return m_categoryId; }

		uint8_t ExpacId() const { return m_expacId; }

		uint8_t PartId() const { return m_partId; }

		uint32_t PathHash() const { return m_pathHash; }

		uint32_t NameHash() const { return m_nameHash; }

		uint32_t FullPathHash() const { return m_fullPathHash; }

		const std::string& Path() const { return m_text; }

		void clear() {
			m_text.clear();
			m_fullPathHash = m_pathHash = m_nameHash = EmptyHashValue;
		}

		[[nodiscard]] std::string PackExpacName() const {
			if (m_expacId == 0)
				return "ffxiv";
			else
				return std::format("ex{}", m_expacId);
		}

		[[nodiscard]] uint32_t PackNameValue() const {
			return (m_categoryId << 16) | (m_expacId << 8) | m_partId;
		}

		[[nodiscard]] std::string PackName() const {
			return std::format("{:0>6x}", PackNameValue());
		}

		bool operator==(const EntryPathSpec& r) const {
			if (m_empty && r.m_empty)
				return true;

			return m_categoryId == r.m_categoryId
				&& m_expacId == r.m_expacId
				&& m_partId == r.m_partId
				&& m_fullPathHash == r.m_fullPathHash
				&& m_nameHash == r.m_nameHash
				&& m_pathHash == r.m_pathHash
				&& (m_text.empty() || r.m_text.empty() || FullPathComparator::Compare(*this, r) == 0);
		}

		bool operator!=(const EntryPathSpec& r) const {
			return !this->operator==(r);
		}

		struct AllHashComparator {
			static int Compare(const EntryPathSpec& l, const EntryPathSpec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_fullPathHash < r.m_fullPathHash)
					return -1;
				if (l.m_fullPathHash > r.m_fullPathHash)
					return 1;
				if (l.m_pathHash < r.m_pathHash)
					return -1;
				if (l.m_pathHash > r.m_pathHash)
					return 1;
				if (l.m_nameHash < r.m_nameHash)
					return -1;
				if (l.m_nameHash > r.m_nameHash)
					return 1;
				return 0;
			}

			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				return Compare(l, r) < 0;
			}
		};

		struct FullHashComparator {
			static int Compare(const EntryPathSpec& l, const EntryPathSpec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_fullPathHash < r.m_fullPathHash)
					return -1;
				if (l.m_fullPathHash > r.m_fullPathHash)
					return 1;
				return 0;
			}

			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				return Compare(l, r) < 0;
			}
		};

		struct PairHashComparator {
			static int Compare(const EntryPathSpec& l, const EntryPathSpec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_pathHash < r.m_pathHash)
					return -1;
				if (l.m_pathHash > r.m_pathHash)
					return 1;
				if (l.m_nameHash < r.m_nameHash)
					return -1;
				if (l.m_nameHash > r.m_nameHash)
					return 1;
				return 0;
			}

			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				return Compare(l, r) < 0;
			}
		};

		struct FullPathComparator {
			static int Compare(const EntryPathSpec& l, const EntryPathSpec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				for (size_t i = 0; i < l.m_text.size() && i < r.m_text.size(); i++) {
					const auto x = std::tolower(l.m_text[i]);
					const auto y = std::tolower(r.m_text[i]);
					if (x < y)
						return -1;
					if (x > y)
						return 1;
				}
				if (l.m_text.size() < r.m_text.size())
					return -1;
				if (l.m_text.size() > r.m_text.size())
					return 1;
				return 0;
			}

			bool operator()(const EntryPathSpec& l, const EntryPathSpec& r) const {
				return Compare(l, r) < 0;
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
	auto format(const Sqex::Sqpack::EntryPathSpec& t, FormatContext& fc) {
		return std::formatter<std::basic_string<char>, char>::format(std::format("{}({:08x}/{:08x}, {:08x})", t.m_text.wstring(), t.m_pathHash, t.m_nameHash, t.m_fullPathHash), fc);
	}
};
