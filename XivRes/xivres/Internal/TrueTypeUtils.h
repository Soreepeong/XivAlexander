#ifndef _XIVRES_FONTGENERATOR_TRUETYPEUTILS_H_
#define _XIVRES_FONTGENERATOR_TRUETYPEUTILS_H_

#include <algorithm>
#include <map>
#include <set>
#include <span>

#include "ByteOrder.h"
#include "SpanCast.h"

#pragma pack(push, 2)
namespace XivRes::Internal::TrueType {
	union TagStruct {
		char Tag[4];
		uint32_t IntValue;

		bool operator==(const TagStruct& r) const {
			return IntValue == r.IntValue;
		}
	};

	struct Fixed {
		BE<uint16_t> Major;
		BE<uint16_t> Minor;
	};
	static_assert(sizeof Fixed == 0x04);

	struct OffsetTableStruct {
		Fixed SfntVersion;
		BE<uint16_t> TableCount;
		BE<uint16_t> SearchRange;
		BE<uint16_t> EntrySelector;
		BE<uint16_t> RangeShift;
	};
	static_assert(sizeof OffsetTableStruct == 0x0C);

	struct DirectoryTableEntry {
		TagStruct Tag;
		BE<uint32_t> Checksum;
		BE<uint32_t> Offset;
		BE<uint32_t> Length;
	};
	static_assert(sizeof DirectoryTableEntry == 0x10);

	struct Head {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/head
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6head.html

		static constexpr TagStruct DirectoryTableTag{ { 'h', 'e', 'a', 'd' } };
		static constexpr uint32_t MagicNumberValue = 0x5F0F3CF5;

		enum class HeadFlags : uint16_t {
			BaselineForFontAtZeroY = 1 << 0,
			LeftSideBearingAtZeroX = 1 << 1,
			InstructionsDependOnPointSize = 1 << 2,
			ForcePpemsInteger = 1 << 3,
			InstructionsAlterAdvanceWidth = 1 << 4,
			VerticalLayout = 1 << 5,
			Reserved6 = 1 << 6,
			RequiresLayoutForCorrectLinguisticRendering = 1 << 7,
			IsAatFont = 1 << 8,
			ContainsRtlGlyph = 1 << 9,
			ContainsIndicStyleRearrangementEffects = 1 << 10,
			Lossless = 1 << 11,
			ProduceCompatibleMetrics = 1 << 12,
			OptimizedForClearType = 1 << 13,
			IsLastResortFont = 1 << 14,
			Reserved15 = 1 << 15,
		};

		enum class MacStyleFlags : uint16_t {
			Bold = 1 << 0,
			Italic = 1 << 1,
			Underline = 1 << 2,
			Outline = 1 << 3,
			Shadow = 1 << 4,
			Condensed = 1 << 5,
			Extended = 1 << 6,
		};

		Fixed Version;
		Fixed FontRevision;
		BE<uint32_t> ChecksumAdjustment;
		BE<uint32_t> MagicNumber;
		BE<uint16_t> Flags;
		BE<uint16_t> UnitsPerEm;
		BE<uint64_t> CreatedTimestamp;
		BE<uint64_t> ModifiedTimestamp;
		BE<int16_t> MinX;
		BE<int16_t> MinY;
		BE<int16_t> MaxX;
		BE<int16_t> MaxY;
		BE<MacStyleFlags> MacStyle;
		BE<uint16_t> LowestRecommendedPpem;
		BE<int16_t> FontDirectionHint;
		BE<int16_t> IndexToLocFormat;
		BE<int16_t> GlyphDataFormat;

		static const Head* TryCast(const void* pData, size_t length) {
			const auto pHead = reinterpret_cast<const Head*>(pData);

			if (sizeof(*pHead) > length)
				return nullptr;
			if (pHead->Version.Major != 1)
				return nullptr;
			if (pHead->MagicNumber != MagicNumberValue)
				return nullptr;
			return pHead;
		}
	};

	inline constexpr Head::HeadFlags operator|(Head::HeadFlags a, Head::HeadFlags b) {
		return static_cast<Head::HeadFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
	}

	inline constexpr Head::HeadFlags operator&(Head::HeadFlags a, Head::HeadFlags b) {
		return static_cast<Head::HeadFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
	}

	inline constexpr Head::HeadFlags operator~(Head::HeadFlags a) {
		return static_cast<Head::HeadFlags>(~static_cast<uint16_t>(a));
	}

	inline constexpr Head::MacStyleFlags operator|(Head::MacStyleFlags a, Head::MacStyleFlags b) {
		return static_cast<Head::MacStyleFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
	}

	inline constexpr Head::MacStyleFlags operator&(Head::MacStyleFlags a, Head::MacStyleFlags b) {
		return static_cast<Head::MacStyleFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
	}

	inline constexpr Head::MacStyleFlags operator~(Head::MacStyleFlags a) {
		return static_cast<Head::MacStyleFlags>(~static_cast<uint16_t>(a));
	}

	enum class PlatformId : uint16_t {
		Unicode = 0,
		Macintosh = 1,  // discouraged
		Iso = 2,  // deprecated
		Windows = 3,
		Custom = 4,  // OTF Windows NT compatibility mappingv
	};

	enum class UnicodePlatformEncodingId : uint16_t {
		Unicode_1_0 = 0,  // deprecated
		Unicode_1_1 = 1,  // deprecated
		IsoIec_10646 = 2,  // deprecated
		Unicode_2_0_Bmp = 3,
		Unicode_2_0_Full = 4,
		UnicodeVariationSequences = 5,
		UnicodeFullRepertoire = 6,
	};

	enum class MacintoshPlatformEncodingId : uint16_t {
		Roman = 0,
	};

	enum class IsoPlatformEncodingId : uint16_t {
		Ascii = 0,
		Iso_10646 = 1,
		Iso_8859_1 = 2,
	};

	enum class WindowsPlatformEncodingId : uint16_t {
		Symbol = 0,
		UnicodeBmp = 1,
		ShiftJis = 2,
		Prc = 3,
		Big5 = 4,
		Wansung = 5,
		Johab = 6,
		UnicodeFullRepertoire = 10,
	};

	struct Name {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/name
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6name.html

		static constexpr TagStruct DirectoryTableTag{ { 'n', 'a', 'm', 'e' } };

		struct NameHeader {
			BE<uint16_t> Version;
			BE<uint16_t> Count;
			BE<uint16_t> StorageOffset;
		};

		struct NameRecord {
			BE<PlatformId> Platform;
			union {
				BE<uint16_t> EncodingId;
				BE<UnicodePlatformEncodingId> UnicodeEncoding;
				BE<MacintoshPlatformEncodingId> MacintoshEncoding;
				BE<IsoPlatformEncodingId> IsoEncoding;
				BE<WindowsPlatformEncodingId> WindowsEncoding;
			};
			BE<uint16_t> LanguageId;
			BE<uint16_t> NameId;
			BE<uint16_t> Length;
			BE<uint16_t> StringOffset;
		};

		struct LanguageHeader {
			BE<uint16_t> Count;
		};

		struct LanguageRecord {
			BE<uint16_t> Length;
			BE<uint16_t> LanguageTagOffset;
		};

		NameHeader Header;
		NameRecord Record[1];

		std::span<const NameRecord> RecordSpan() const { return { Record, *Header.Count }; }

		uint16_t LanguageCount() const { return *Header.Version >= 1 ? **reinterpret_cast<const BE<uint16_t>*>(&Record[*Header.Count]) : 0; }
		std::span<const LanguageRecord> LanguageSpan() const {
			if (*Header.Version == 0)
				return {};
			return { reinterpret_cast<const LanguageRecord*>(reinterpret_cast<const char*>(&Record[*Header.Count]) + 2), LanguageCount() };
		}
		const LanguageRecord& Language(size_t i) {
			return reinterpret_cast<const LanguageRecord*>(reinterpret_cast<const char*>(&Record[*Header.Count]) + 2)[i];
		}

		static const Name* TryCast(const void* pData, size_t length) {
			const auto pName = reinterpret_cast<const Name*>(pData);
			if (length < sizeof NameHeader)
				return nullptr;

			return pName;
		}

		std::u8string GetUnicodeName(uint16_t preferredLanguageId, uint16_t nameId, size_t tableSize) const {
			const auto recordSpans = RecordSpan();
			for (const auto usePreferredLanguageId : { true, false }) {
				for (const auto& record : recordSpans) {
					if (record.LanguageId != preferredLanguageId && usePreferredLanguageId)
						continue;

					if (record.NameId != nameId)
						continue;

					const auto offsetStart = static_cast<size_t>(*Header.StorageOffset) + *record.StringOffset;
					const auto offsetEnd = offsetStart + *record.Length;
					if (offsetEnd > tableSize)
						continue;

					switch (record.Platform) {
						case PlatformId::Unicode:
							switch (*record.UnicodeEncoding) {
								case UnicodePlatformEncodingId::Unicode_2_0_Bmp:
								case UnicodePlatformEncodingId::Unicode_2_0_Full:
									goto decode_utf16be;
							}
							break;

						case PlatformId::Macintosh:
							switch (*record.MacintoshEncoding) {
								case MacintoshPlatformEncodingId::Roman:
									goto decode_ascii;
							}
							break;

						case PlatformId::Windows:
							switch (*record.WindowsEncoding) {
								case WindowsPlatformEncodingId::Symbol:
								case WindowsPlatformEncodingId::UnicodeBmp:
								case WindowsPlatformEncodingId::UnicodeFullRepertoire:
									goto decode_utf16be;
							}
							break;
					}

					continue;

				decode_ascii:
					{
						const auto pString = reinterpret_cast<const char*>(reinterpret_cast<const char*>(this) + offsetStart);
						const auto pStringEnd = reinterpret_cast<const char*>(reinterpret_cast<const char*>(this) + offsetEnd);
						std::u8string res;
						res.reserve(pStringEnd - pString);
						for (auto x = pString; x < pStringEnd; x++) {
							if (0 < *x && *x < 0x80 && *x != '\\') {
								res.push_back(*x);
							} else if (*x == '\\') {
								res.push_back('\\');
								res.push_back('\\');
							} else {
								res.push_back('\\');
								res.push_back('x');
								res.push_back(((*x >> 4) > 10) ? ('A' + (*x >> 4) - 10) : ('0' + (*x >> 4)));
								res.push_back(((*x & 0xF) > 10) ? ('A' + (*x & 0xF) - 10) : ('0' + (*x & 0xF)));
							}
						}
						return res;
					}

				decode_utf16be:
					{
						const auto pString = reinterpret_cast<const BE<char16_t>*>(reinterpret_cast<const char*>(this) + offsetStart);
						const auto pStringEnd = reinterpret_cast<const BE<char16_t>*>(reinterpret_cast<const char*>(this) + offsetEnd);
						std::u16string u16;
						u16.reserve(pStringEnd - pString);
						for (auto x = pString; x < pStringEnd; x++)
							u16.push_back(*x);

						std::u8string u8;
						u8.reserve(4 * u16.size());
						for (auto ptr = &u16[0]; ptr < &u16[0] + u16.size();) {
							size_t remaining = &u16[0] + u16.size() - ptr;
							const auto c = Internal::DecodeUtf16(ptr, remaining);
							ptr += remaining;

							const auto u8p = u8.size();
							u8.resize(u8p + Internal::EncodeUtf8Length(c));
							Internal::EncodeUtf8(&u8[u8p], c);
						}

						return u8;
					}
				}
			}
			return {};
		}
	};

	struct Cmap {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/cmap
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6cmap.html

		static constexpr TagStruct DirectoryTableTag{ { 'c', 'm', 'a', 'p' } };

		struct TableHeader {
			BE<uint16_t> Version;
			BE<uint16_t> SubtableCount;
		};

		struct EncodingRecord {
			BE<PlatformId> Platform;
			union {
				BE<uint16_t> EncodingId;
				BE<UnicodePlatformEncodingId> UnicodeEncoding;
				BE<MacintoshPlatformEncodingId> MacintoshEncoding;
				BE<IsoPlatformEncodingId> IsoEncoding;
				BE<WindowsPlatformEncodingId> WindowsEncoding;
			};
			BE<uint32_t> SubtableOffset;
		};

		union Format {
			BE<uint16_t> FormatId;

			struct Format0 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
				};

				Header FormatHeader;
				uint8_t GlyphIdArray[256];

				uint16_t CharToGlyph(uint32_t c) const {
					return c >= 256 ? 0 : GlyphIdArray[c];
				}

				static const Format0* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format0*>(pData);
					if (length < sizeof Format0 || *pFormat->FormatHeader.FormatId != 0 || length < *pFormat->FormatHeader.Length)
						return nullptr;

					return static_cast<const Format0*>(pData);
				}

				void Parse(std::vector<char32_t>& result) const {
					for (char32_t i = 0; i < 256; i++)
						if (GlyphIdArray[i])
							result[GlyphIdArray[i]] = i;
				}
			};

			struct Format2 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> SubHeaderKeys[256];
				};

				struct SubHeader {
					BE<uint16_t> FirstCode;
					BE<uint16_t> EntryCount;
					BE<int16_t> IdDelta;
					BE<uint16_t> IdRangeOffset;
				};

				Header FormatHeader;
				SubHeader SubHeaders[1];

				uint16_t CharToGlyph(uint32_t c) const {
					if (c >= 0x10000)
						return 0;

					const auto& subHeader = SubHeaders[FormatHeader.SubHeaderKeys[c >> 8] / sizeof SubHeader];
					if (reinterpret_cast<const char*>(&subHeader) + sizeof(subHeader) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
						return 0; // overflow

					c = c & 0xFF;
					if (c < *subHeader.FirstCode || c >= static_cast<uint32_t>(*subHeader.FirstCode + *subHeader.EntryCount))
						return 0;

					const auto glyphArray = reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(&subHeader.IdRangeOffset) + sizeof subHeader.IdRangeOffset + subHeader.IdRangeOffset);
					const auto pGlyphIndex = &glyphArray[c - subHeader.FirstCode];
					if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
						return 0; // overflow

					c = **pGlyphIndex;
					return c == 0 ? 0 : (c + subHeader.IdDelta) & 0xFFFF;
				}

				static const Format2* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format2*>(pData);
					if (length < sizeof Header || *pFormat->FormatHeader.FormatId != 2 || length < *pFormat->FormatHeader.Length)
						return nullptr;

					return pFormat;
				}

				void Parse(std::vector<char32_t>& result) const {
					for (char32_t c = 0; c < 0x10000; c += 0x100) {
						const auto& subHeader = SubHeaders[FormatHeader.SubHeaderKeys[c >> 8] / sizeof SubHeader];
						if (reinterpret_cast<const char*>(&subHeader) + sizeof(subHeader) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
							continue;  // overflow

						const auto glyphArray = reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(&subHeader.IdRangeOffset) + sizeof subHeader.IdRangeOffset + subHeader.IdRangeOffset);
						for (char32_t c2 = c + subHeader.FirstCode, c2_ = c2 + subHeader.EntryCount; c2 < c2_; c2++) {
							const auto pGlyphIndex = &glyphArray[c - subHeader.FirstCode];
							if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
								continue; // overflow

							const auto glyphIndex = **pGlyphIndex;
							if (glyphIndex)
								result[(c + subHeader.IdDelta) & 0xFFFF] = c2;
						}
					}
				}
			};

			struct Format4 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> SegCountX2;
					BE<uint16_t> SearchRange;
					BE<uint16_t> EntrySelector;
					BE<uint16_t> RangeShift;
				};

				Header FormatHeader;
				char VariableData[1];
				std::span<const BE<uint16_t>> EndCodeSpan() const { return { reinterpret_cast<const BE<uint16_t>*>(&VariableData[0]), static_cast<size_t>(*FormatHeader.SegCountX2 / 2) }; }
				std::span<const BE<uint16_t>> StartCodeSpan() const { return { reinterpret_cast<const BE<uint16_t>*>(&VariableData[2 + *FormatHeader.SegCountX2 * 1]), static_cast<size_t>(*FormatHeader.SegCountX2 / 2) }; }
				std::span<const BE<uint16_t>> IdDeltaSpan() const { return { reinterpret_cast<const BE<uint16_t>*>(&VariableData[2 + *FormatHeader.SegCountX2 * 2]), static_cast<size_t>(*FormatHeader.SegCountX2 / 2) }; }
				std::span<const BE<uint16_t>> IdRangeSpan() const { return { reinterpret_cast<const BE<uint16_t>*>(&VariableData[2 + *FormatHeader.SegCountX2 * 3]), static_cast<size_t>(*FormatHeader.SegCountX2 / 2) }; }

				const BE<uint16_t>& EndCode(size_t i) const { return reinterpret_cast<const BE<uint16_t>*>(&VariableData[0])[i]; }
				const BE<uint16_t>& StartCode(size_t i) const { return reinterpret_cast<const BE<uint16_t>*>(&VariableData[2 + *FormatHeader.SegCountX2 * 1])[i]; }
				const BE<uint16_t>& IdDelta(size_t i) const { return reinterpret_cast<const BE<uint16_t>*>(&VariableData[2 + *FormatHeader.SegCountX2 * 2])[i]; }
				const BE<uint16_t>& IdRangeOffset(size_t i) const { return reinterpret_cast<const BE<uint16_t>*>(&VariableData[2 + *FormatHeader.SegCountX2 * 3])[i]; }

				uint16_t CharToGlyph(uint32_t c) const {
					if (c >= 0x10000)
						return 0;

					const auto bec = BE<uint16_t>(static_cast<uint16_t>(c));
					const auto i = std::ranges::upper_bound(EndCodeSpan(), bec, [](const auto& l, const auto& r) { return *l < *r; }) - EndCodeSpan().begin() - 1;
					if (i < 0)
						return 0;

					const auto startCode = *StartCode(i);
					if (c < startCode || c > EndCode(i))
						return 0;

					const auto pIdRangeOffset = &IdRangeOffset(i);
					if (reinterpret_cast<const char*>(pIdRangeOffset) + sizeof(*pIdRangeOffset) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
						return 0; // overflow

					const auto idRangeOffset = **pIdRangeOffset;
					const auto idDelta = *IdDelta(i);
					if (idRangeOffset == 0)
						return (idDelta + c) & 0xFFFF;

					const auto pGlyphIndex = &pIdRangeOffset[idRangeOffset / 2 + c - startCode];
					if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
						return 0; // overflow

					const auto glyphIndex = **pGlyphIndex;
					return glyphIndex == 0 ? 0 : (idDelta + glyphIndex) & 0xFFFF;
				}

				static const Format4* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format4*>(pData);
					if (length < sizeof Header || *pFormat->FormatHeader.FormatId != 4 || length < *pFormat->FormatHeader.Length)
						return nullptr;

					if (reinterpret_cast<const char*>(pFormat->IdRangeSpan().data() + pFormat->IdRangeSpan().size()) > static_cast<const char*>(pData) + length)
						return nullptr;

					return pFormat;
				}

				void Parse(std::vector<char32_t>& result) const {
					for (size_t i = 0, i_ = FormatHeader.SegCountX2 / 2; i < i_; i++) {
						const auto pIdRangeOffset = &IdRangeOffset(i);
						if (reinterpret_cast<const char*>(pIdRangeOffset) + sizeof(*pIdRangeOffset) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
							continue; // overflow

						const auto idRangeOffset = **pIdRangeOffset;
						const auto idDelta = *IdDelta(i);

						if (idRangeOffset == 0) {
							for (char32_t startCode = *StartCode(i), endCode = *EndCode(i), c = startCode; c <= endCode; c++)
								result[(idDelta + c) & 0xFFFF] = c;

						} else {
							for (char32_t startCode = *StartCode(i), endCode = *EndCode(i), c = startCode; c <= endCode; c++) {
								const auto pGlyphIndex = &pIdRangeOffset[idRangeOffset / 2 + c - startCode];
								if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > reinterpret_cast<const char*>(this) + *FormatHeader.Length)
									break; // overflow

								const auto glyphIndex = **pGlyphIndex;
								if (glyphIndex)
									result[(idDelta + glyphIndex) & 0xFFFF] = c;
							}
						}
					}
				}
			};

			struct Format6 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> FirstCode;
					BE<uint16_t> EntryCount;
				};

				Header FormatHeader;
				BE<uint16_t> GlyphId[1];
				std::span<const BE<uint16_t>> GlyphIdSpan() const { return { GlyphId, *FormatHeader.EntryCount }; }

				uint16_t CharToGlyph(uint32_t c) const {
					if (c < *FormatHeader.FirstCode || c >= static_cast<uint32_t>(*FormatHeader.FirstCode + *FormatHeader.EntryCount))
						return 0;

					return GlyphId[c - FormatHeader.FirstCode];
				}

				static const Format6* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format6*>(pData);
					if (length < sizeof Header || *pFormat->FormatHeader.FormatId != 6 || length < *pFormat->FormatHeader.Length)
						return nullptr;

					if (reinterpret_cast<const char*>(&pFormat->GlyphId[*pFormat->FormatHeader.EntryCount]) > static_cast<const char*>(pData) + length)
						return nullptr;

					return pFormat;
				}

				void Parse(std::vector<char32_t>& result) const {
					for (char32_t c = *FormatHeader.FirstCode, c_ = c + *FormatHeader.EntryCount; c < c_; c++)
						if (const auto glyphId = *GlyphId[c])
							result[glyphId] = c;
				}
			};

			struct SequentialMapGroup {
				BE<uint32_t> StartCharCode;
				BE<uint32_t> EndCharCode;
				BE<uint32_t> StartGlyphId;
			};

			struct ConstantMapGroup {
				BE<uint32_t> StartCharCode;
				BE<uint32_t> EndCharCode;
				BE<uint32_t> GlyphId;
			};

			struct Format8 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					uint8_t Is32[8192];
					BE<uint32_t> GroupCount;
				};

				Header FormatHeader;
				SequentialMapGroup Group[1];
				std::span<const SequentialMapGroup> GroupSpan() const { return { Group, static_cast<size_t>(*FormatHeader.GroupCount) }; }

				uint16_t CharToGlyph(uint32_t c) const {
					SequentialMapGroup tmp;
					tmp.EndCharCode = c;
					const auto& group = std::upper_bound(GroupSpan().begin(), GroupSpan().end(), tmp, [](const SequentialMapGroup& l, const SequentialMapGroup& r) { return *l.EndCharCode < *r.EndCharCode; })[-1];
					if (&group < Group || c < *group.StartCharCode || c > *group.EndCharCode)
						return 0;

					return group.StartGlyphId + c - group.StartCharCode;
				}

				static const Format8* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format8*>(pData);
					if (length < sizeof Header || *pFormat->FormatHeader.FormatId != 8 || length < *pFormat->FormatHeader.Length)
						return nullptr;

					if (reinterpret_cast<const char*>(&pFormat->Group[*pFormat->FormatHeader.GroupCount]) > static_cast<const char*>(pData) + length)
						return nullptr;

					return pFormat;
				}

				void Parse(std::vector<char32_t>& result) const {
					for (const auto& group : GroupSpan())
						for (char32_t c = *group.StartCharCode, c_ = *group.EndCharCode; c <= c_; c++)
							result[group.StartGlyphId + c - group.StartCharCode] = c;
				}
			};

			struct Format10 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					BE<uint32_t> FirstCode;
					BE<uint32_t> EntryCount;
				};

				Header FormatHeader;
				BE<uint16_t> GlyphId[1];
				std::span<const BE<uint16_t>> GlyphIdSpan() const { return { GlyphId, *FormatHeader.EntryCount }; }

				uint16_t CharToGlyph(uint32_t c) const {
					if (c < FormatHeader.FirstCode || c >= FormatHeader.FirstCode + FormatHeader.EntryCount)
						return 0;

					return GlyphId[c];
				}

				static const Format10* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format10*>(pData);
					if (length < sizeof Header || *pFormat->FormatHeader.FormatId != 10 || length < *pFormat->FormatHeader.Length)
						return nullptr;

					if (reinterpret_cast<const char*>(&pFormat->GlyphId[*pFormat->FormatHeader.EntryCount]) > static_cast<const char*>(pData) + length)
						return nullptr;

					return pFormat;
				}

				void Parse(std::vector<char32_t>& result) const {
					for (char32_t c = *FormatHeader.FirstCode, c_ = c + *FormatHeader.EntryCount; c < c_; c++)
						if (const auto glyphId = *GlyphId[c])
							result[glyphId] = c;
				}
			};

			struct Format12And13 {
				struct Header {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					BE<uint32_t> GroupCount;
				};

				Header FormatHeader;
				union {
					SequentialMapGroup SequentialGroup[1];
					ConstantMapGroup ConstantGroup[1];
				};
				std::span<const SequentialMapGroup> SequentialGroupSpan() const { return { SequentialGroup, *FormatHeader.GroupCount }; }
				std::span<const ConstantMapGroup> ConstantGroupSpan() const { return { ConstantGroup, *FormatHeader.GroupCount }; }

				uint16_t CharToGlyph(uint32_t c) const {
					SequentialMapGroup tmp;
					tmp.EndCharCode = c;
					const auto& group = std::upper_bound(SequentialGroupSpan().begin(), SequentialGroupSpan().end(), tmp, [](const SequentialMapGroup& l, const SequentialMapGroup& r) { return *l.EndCharCode < *r.EndCharCode; })[-1];
					if (&group < SequentialGroup || c < *group.StartCharCode || c > *group.EndCharCode)
						return 0;

					if (*FormatHeader.FormatId == 12)
						return group.StartGlyphId + c - group.StartCharCode;
					else
						return group.StartGlyphId;
				}

				static const Format12And13* TryCast(const void* pData, size_t length) {
					const auto pFormat = static_cast<const Format12And13*>(pData);
					if (length < sizeof Header || (*pFormat->FormatHeader.FormatId != 12 && *pFormat->FormatHeader.FormatId != 13) || length < *pFormat->FormatHeader.Length)
						return nullptr;

					if (reinterpret_cast<const char*>(&pFormat->SequentialGroup[*pFormat->FormatHeader.GroupCount]) > static_cast<const char*>(pData) + length)
						return nullptr;

					return pFormat;
				}

				void Parse(std::vector<char32_t>& result) const {
					if (*FormatHeader.FormatId == 12) {
						for (const auto& group : SequentialGroupSpan())
							for (char32_t c = *group.StartCharCode, c_ = *group.EndCharCode; c <= c_; c++)
								result[group.StartGlyphId + c - group.StartCharCode] = c;
					} else {
						for (const auto& group : ConstantGroupSpan())
							for (char32_t c = *group.StartCharCode, c_ = *group.EndCharCode; c <= c_; c++)
								result[group.GlyphId] = c;
					}
				}
			};
		};

		TableHeader CmapHeader;
		EncodingRecord EncodingRecords[1];

		std::vector<char32_t> Parse() const {
			std::vector<char32_t> result;
			result.resize(65536, (std::numeric_limits<char32_t>::max)());

			for (size_t i = 0, i_ = *CmapHeader.SubtableCount; i < i_; ++i) {
				const auto& encRec = EncodingRecords[i];
				if (encRec.Platform == PlatformId::Unicode)
					void();
				else if (encRec.Platform == PlatformId::Windows && encRec.WindowsEncoding == WindowsPlatformEncodingId::UnicodeBmp)
					void();
				else if (encRec.Platform == PlatformId::Windows && encRec.WindowsEncoding == WindowsPlatformEncodingId::UnicodeFullRepertoire)
					void();
				else
					continue;

				const auto& nFormat = *reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(this) + *encRec.SubtableOffset);
				switch (*nFormat) {
					case 0:
						reinterpret_cast<const Format::Format0*>(&nFormat)->Parse(result);
						break;

					case 2:
						reinterpret_cast<const Format::Format2*>(&nFormat)->Parse(result);
						break;

					case 4:
						reinterpret_cast<const Format::Format4*>(&nFormat)->Parse(result);
						break;

					case 6:
						reinterpret_cast<const Format::Format6*>(&nFormat)->Parse(result);
						break;

					case 8:
						reinterpret_cast<const Format::Format8*>(&nFormat)->Parse(result);
						break;

					case 10:
						reinterpret_cast<const Format::Format10*>(&nFormat)->Parse(result);
						break;

					case 12:
					case 13:
						reinterpret_cast<const Format::Format12And13*>(&nFormat)->Parse(result);
						break;
				}
			}

			return result;
		}

		static const Cmap* TryCast(const void* pData, size_t length) {
			const auto pCmap = reinterpret_cast<const Cmap*>(pData);
			if (length < sizeof(*pCmap))
				return nullptr;
			if (length < sizeof(OffsetTableStruct) + sizeof(EncodingRecord) * pCmap->CmapHeader.SubtableCount)
				return nullptr;

			for (size_t i = 0, i_ = *pCmap->CmapHeader.SubtableCount; i < i_; ++i) {
				const auto& encRec = pCmap->EncodingRecords[i];
				if (encRec.SubtableOffset + sizeof uint16_t > length)
					return nullptr;

				const auto remainingSpan = std::span(static_cast<const char*>(pData), length).subspan(*encRec.SubtableOffset);
				const auto nFormat = **reinterpret_cast<const BE<uint16_t>*>(static_cast<const char*>(pData) + *encRec.SubtableOffset);
				switch (nFormat) {
					case 0:
						if (!Format::Format0::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 2:
						if (!Format::Format2::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 4:
						if (!Format::Format4::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 6:
						if (!Format::Format6::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 8:
						if (!Format::Format8::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 10:
						if (!Format::Format10::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 12:
					case 13:
						if (!Format::Format12And13::TryCast(&remainingSpan[0], remainingSpan.size_bytes()))
							return nullptr;
						break;

					case 14:
						// valid but unsupported, so skip it
						break;

					default:
						return nullptr;
				}
			}

			return pCmap;
		}
	};

	struct Kern {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/kern
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6kern.html

		static constexpr TagStruct DirectoryTableTag{ { 'k', 'e', 'r', 'n' } };

		struct Format0 {
			struct Header {
				BE<uint16_t> PairCount;
				BE<uint16_t> SearchRange;
				BE<uint16_t> EntrySelector;
				BE<uint16_t> RangeShift;
			};

			struct Pair {
				BE<uint16_t> Left;
				BE<uint16_t> Right;
				BE<int16_t> Value;
			};

			Header FormatHeader;
			Pair Pairs[1];

			static const Format0* TryCast(const void* pData, size_t length) {
				if (length < sizeof Header)
					return nullptr;

				const auto pFormat = static_cast<const Format0*>(pData);
				if (reinterpret_cast<const char*>(&pFormat->Pairs[pFormat->FormatHeader.PairCount]) > static_cast<const char*>(pData) + length)
					return nullptr;

				return pFormat;
			}

			void Parse(
				std::map<std::pair<char32_t, char32_t>, int>& result,
				const std::vector<char32_t>& GlyphIndexToCharCodeMap,
				bool cumulative
			) const {
				for (auto pPair = Pairs, pPair_ = Pairs + FormatHeader.PairCount; pPair < pPair_; pPair++) {
					const auto l = GlyphIndexToCharCodeMap[*pPair->Left];
					if (l == (std::numeric_limits<char32_t>::max)())
						continue;

					const auto r = GlyphIndexToCharCodeMap[*pPair->Right];
					if (r == (std::numeric_limits<char32_t>::max)())
						continue;

					auto& target = result[std::make_pair(l, r)];
					if (cumulative)
						target += *pPair->Value;
					else
						target = *pPair->Value;
				}
			}
		};

		struct Version0 {
			struct TableHeader {
				BE<uint16_t> Version;
				BE<uint16_t> SubtableCount;
			};

			struct CoverageBitpacked {
				uint16_t Horizontal : 1;
				uint16_t Minimum : 1;
				uint16_t CrossStream : 1;
				uint16_t Override : 1;
				uint16_t Reserved1 : 4;
				uint16_t Format : 8;
			};

			struct SubtableHeader {
				BE<uint16_t> Version;
				BE<uint16_t> Length;
				BE<CoverageBitpacked> Coverage;
			};

			static void Parse(
				std::map<std::pair<char32_t, char32_t>, int>& result,
				std::span<const char> data,
				const std::vector<char32_t>& GlyphIndexToCharCodeMap
			) {
				const auto& tableHeader = *reinterpret_cast<const TableHeader*>(data.data());
				data = data.subspan(sizeof tableHeader);

				for (size_t i = 0; i < tableHeader.SubtableCount; ++i) {
					if (data.size_bytes() < sizeof SubtableHeader)
						return;  // invalid kern table

					const auto& kernSubtableHeader = *reinterpret_cast<const SubtableHeader*>(data.data());
					if (data.size_bytes() < kernSubtableHeader.Length)
						return;  // invalid kern table

					const auto coverage = *kernSubtableHeader.Coverage;
					if (kernSubtableHeader.Version == 0 && coverage.Horizontal) {
						const auto formatData = data.subspan(sizeof kernSubtableHeader, kernSubtableHeader.Length - sizeof kernSubtableHeader);
						switch (coverage.Format) {
							case 0:
								if (const auto pFormat = Format0::TryCast(&formatData[0], formatData.size_bytes()))
									pFormat->Parse(result, GlyphIndexToCharCodeMap, !coverage.Override);
								break;

							default:
								__debugbreak();
						}
					}

					data = data.subspan(kernSubtableHeader.Length);
				}
			}
		};

		struct Version1 {
			struct TableHeader {
				BE<uint32_t> Version;
				BE<uint32_t> SubtableCount;
			};

			struct Coverage {
				uint16_t Vertical : 1;
				uint16_t CrossStream : 1;
				uint16_t Variation : 1;
				uint16_t Reserved1 : 5;
				uint16_t Format : 8;
			};

			struct SubtableHeader {
				BE<uint32_t> Length;
				BE<Coverage> Coverage;
				BE<uint16_t> TupleIndex;
			};

			static void Parse(
				std::map<std::pair<char32_t, char32_t>, int>& result,
				std::span<const char> data,
				const std::vector<char32_t>& GlyphIndexToCharCodeMap
			) {
				// Untested

				const auto& tableHeader = *reinterpret_cast<const TableHeader*>(data.data());
				if (tableHeader.Version == 0x10000 && data.size_bytes() >= 8) {
					data = data.subspan(sizeof tableHeader);
					for (size_t i = 0; i < tableHeader.SubtableCount; ++i) {
						if (data.size_bytes() < sizeof SubtableHeader)
							return;  // invalid kern table

						const auto& kernSubtableHeader = *reinterpret_cast<const SubtableHeader*>(data.data());
						if (data.size_bytes() < kernSubtableHeader.Length)
							return;  // invalid kern table

						const auto coverage = *kernSubtableHeader.Coverage;
						if (!coverage.Vertical) {
							const auto formatData = data.subspan(sizeof kernSubtableHeader, kernSubtableHeader.Length - sizeof kernSubtableHeader);
							switch (coverage.Format) {
								case 0:
									if (const auto pFormat = Format0::TryCast(&formatData[0], formatData.size_bytes()))
										pFormat->Parse(result, GlyphIndexToCharCodeMap, false);
									break;

								default:
									__debugbreak();
							}
						}

						data = data.subspan(kernSubtableHeader.Length);
					}
				}
			}
		};

		std::map<std::pair<char32_t, char32_t>, int> Parse(const std::vector<char32_t>& glyphIndexToCharCodeMap, size_t length) const {
			std::map<std::pair<char32_t, char32_t>, int> result;

			switch (reinterpret_cast<const Version0::TableHeader*>(this)->Version) {
				case 0:
					Version0::Parse(result, std::span(reinterpret_cast<const char*>(this), length), glyphIndexToCharCodeMap);
					break;

				case 1:
					Version1::Parse(result, std::span(reinterpret_cast<const char*>(this), length), glyphIndexToCharCodeMap);
					break;
			}

			return result;
		}

		static const Kern* TryCast(const void* pData, size_t length) {
			const auto pKern = reinterpret_cast<const Kern*>(pData);
			if (length < sizeof Version0::TableHeader)
				return nullptr;

			return pKern;
		}
	};

	// http://formats.kaitai.io/ttf/ttf.svg
	struct SfntFileView {
		SfntFileView() = delete;
		SfntFileView(SfntFileView&&) = delete;
		SfntFileView(const SfntFileView&) = delete;
		SfntFileView& operator=(SfntFileView&&) = delete;
		SfntFileView& operator=(const SfntFileView&) = delete;

		OffsetTableStruct OffsetTable;
		DirectoryTableEntry DirectoryTable[1];

		std::span<const DirectoryTableEntry> DirectoryTableSpan() const {
			return { DirectoryTable, *OffsetTable.TableCount };
		}

		std::span<const char> GetDirectoryTable(const TagStruct& tag, size_t nOffsetInTtc = 0)  const {
			for (const auto& table : DirectoryTableSpan())
				if (table.Tag == tag)
					return { reinterpret_cast<const char*>(this) + *table.Offset - nOffsetInTtc, *table.Length };
			return {};
		}

		template<typename Table>
		std::pair<const Table*, size_t> TryGetTable(size_t offsetInTtc = 0) const {
			const auto s = GetDirectoryTable(Table::DirectoryTableTag, offsetInTtc);
			return std::make_pair<const Table*, size_t>(s.empty() ? nullptr : Table::TryCast(&s[0], s.size_bytes()), s.size_bytes());
		}

		size_t RequiredLength() const {
			size_t offset = sizeof(OffsetTableStruct) + sizeof(DirectoryTableEntry) * *OffsetTable.TableCount;
			for (size_t i = 0, i_ = *OffsetTable.TableCount; i < i_; i++)
				offset = (std::max)(offset, static_cast<size_t>(*DirectoryTable[i].Offset) + *DirectoryTable[i].Length);
			return offset;
		}

		bool IsValid(size_t length) const {
			if (length < sizeof OffsetTable)
				return false;
			if (length < sizeof OffsetTable + sizeof DirectoryTableEntry * OffsetTable.TableCount)
				return false;
			if (RequiredLength() > length)
				return false;
			return true;
		}

		static const SfntFileView* TryCast(const void* pData, size_t length) {
			const auto p = static_cast<const SfntFileView*>(pData);
			if (length < sizeof SfntFileView || !p->IsValid(length))
				return nullptr;

			return p;
		}
	};

	struct TtcFileView {
		TtcFileView() = delete;
		TtcFileView(TtcFileView&&) = delete;
		TtcFileView(const TtcFileView&) = delete;
		TtcFileView& operator=(TtcFileView&&) = delete;
		TtcFileView& operator=(const TtcFileView&) = delete;

		struct Header {
			static constexpr TagStruct HeaderTag{ { 't', 't', 'c', 'f' } };

			TagStruct Tag;
			BE<uint16_t> MajorVersion;
			BE<uint16_t> MinorVersion;
			BE<uint32_t> FontCount;
		};

		struct DigitalSignatureHeader {
			static constexpr TagStruct HeaderTag{ { 'D', 'S', 'I', 'G' } };

			TagStruct Tag;
			BE<uint32_t> Length;
			BE<uint32_t> Offset;
		};

		Header FileHeader;
		BE<uint32_t> FontOffsets[1];

		const SfntFileView& GetFont(size_t index) const {
			if (index >= FileHeader.FontCount)
				throw std::out_of_range("Font index of range");

			return *reinterpret_cast<const SfntFileView*>(reinterpret_cast<const char*>(this) + FontOffsets[index]);
		}

		bool IsValid(size_t length) const {
			if (length < sizeof Header)
				return false;
			if (FileHeader.Tag != Header::HeaderTag)
				return false;
			if (FileHeader.MajorVersion == 0)
				return false;
			if (length < sizeof Header + sizeof uint32_t * FileHeader.FontCount)
				return false;
			if (FileHeader.MajorVersion >= 2) {
				if (length < sizeof Header + sizeof uint32_t * FileHeader.FontCount + sizeof DigitalSignatureHeader)
					return false;

				const auto pDsig = reinterpret_cast<const DigitalSignatureHeader*>(&FontOffsets[*FileHeader.FontCount]);
				if (pDsig->Tag.IntValue == 0)
					void();
				else if (pDsig->Tag == DigitalSignatureHeader::HeaderTag) {
					if (length < static_cast<size_t>(*pDsig->Offset) + *pDsig->Length)
						return false;
				} else
					return false;
			}
			for (size_t i = 0, i_ = *FileHeader.FontCount; i < i_; i++) {
				const auto& font = GetFont(i);
				if (*FontOffsets[i] + font.RequiredLength() > length)
					return false;
			}
			return true;
		}

		static const TtcFileView* TryCast(const void* pData, size_t length) {
			const auto p = static_cast<const TtcFileView*>(pData);
			if (length < sizeof TtcFileView || !p->IsValid(length))
				return nullptr;

			return p;
		}
	};
}
#pragma pack(pop)

#endif
