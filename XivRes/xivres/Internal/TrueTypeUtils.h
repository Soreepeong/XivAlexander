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
		uint32_t NativeValue;
		RNE<uint32_t> ReverseNativeValue;

		bool operator==(const TagStruct& r) const {
			return NativeValue == r.NativeValue;
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

	struct LookupList {
		BE<uint16_t> Count;
		BE<uint16_t> Offsets[1];

		class View {
			union {
				const LookupList* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof uint16_t)
					return;

				if (2 * (*obj->Count + 1) > data.size_bytes())
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::span<const BE<uint16_t>> Offsets() const {
				return { m_obj->Offsets, *m_obj->Count };
			}
		};
	};

	struct LookupTable {
		struct LookupFlags {
			uint8_t RightToLeft : 1;
			uint8_t IgnoreBaseGlyphs : 1;
			uint8_t IgnoreLigatures : 1;
			uint8_t IgnoreMarks : 1;
			uint8_t UseMarkFilteringSet : 1;
			uint8_t Reserved : 3;
		};
		static_assert(sizeof LookupFlags == 1);

		struct LookupTableHeader {
			BE<uint16_t> LookupType;
			uint8_t MarkAttachmentType;
			LookupFlags LookupFlag;
			BE<uint16_t> SubtableCount;
		};

		LookupTableHeader Header;
		BE<uint16_t> SubtableOffsets[1];

		class View {
			union {
				const LookupTable* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof Header)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof Header + static_cast<size_t>(2) * (*obj->Header.SubtableCount) + (obj->Header.LookupFlag.UseMarkFilteringSet ? 2 : 0))
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::span<const BE<uint16_t>> SubtableOffsets() const {
				return { m_obj->SubtableOffsets, *m_obj->Header.SubtableCount };
			}

			std::span<const char> SubtableSpan(size_t index) const {
				const auto offset = *m_obj->SubtableOffsets[index];
				return { m_bytes + offset, m_length - offset };
			}

			uint16_t MarkFilteringSet() const {
				if (m_obj->Header.LookupFlag.UseMarkFilteringSet)
					return m_obj->SubtableOffsets[*m_obj->Header.SubtableCount];
				return (std::numeric_limits<uint16_t>::max)();
			}
		};
	};

	struct CoverageTable {
		struct FormatHeader {
			BE<uint16_t> FormatId;
			BE<uint16_t> Count;
		};

		struct RangeRecord {
			BE<uint16_t> StartGlyphId;
			BE<uint16_t> EndGlyphId;
			BE<uint16_t> StartCoverageIndex;
		};

		FormatHeader Header;
		union {
			BE<uint16_t> Glyphs[1];
			RangeRecord RangeRecords[1];
		};

		class View {
			union {
				const CoverageTable* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof FormatHeader)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				const auto count = static_cast<size_t>(*obj->Header.Count);
				switch (obj->Header.FormatId) {
					case 1:
						if (data.size_bytes() < sizeof FormatHeader + sizeof uint16_t * count)
							return;
						break;

					case 2:
						if (data.size_bytes() < sizeof FormatHeader + sizeof RangeRecord * count)
							return;
						break;

					default:
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::span<const BE<uint16_t>> GlyphSpan() const {
				return { m_obj->Glyphs, m_obj->Header.Count };
			}

			std::span<const RangeRecord> RangeRecordSpan() const {
				return { m_obj->RangeRecords, m_obj->Header.Count };
			}

			size_t GetCoverageIndex(size_t glyphId) const {
				switch (m_obj->Header.FormatId) {
					case 1:
					{
						const auto glyphSpan = GlyphSpan();
						const auto bec = BE<uint16_t>(static_cast<uint16_t>(glyphId));
						const auto it = std::lower_bound(glyphSpan.begin(), glyphSpan.end(), bec, [](const BE<uint16_t>& l, const BE<uint16_t>& r) { return *l < *r; });
						if (it != glyphSpan.end() && **it == glyphId)
							return it - glyphSpan.begin();

						break;
					}

					case 2:
					{
						const auto rangeSpan = RangeRecordSpan();
						const auto bec = RangeRecord{ .EndGlyphId = static_cast<uint16_t>(glyphId) };
						const auto i = std::upper_bound(rangeSpan.begin(), rangeSpan.end(), bec, [](const RangeRecord& l, const RangeRecord& r) { return *l.EndGlyphId < *r.EndGlyphId; }) - rangeSpan.begin() - 1;
						if (i >= 0 && rangeSpan[i].StartGlyphId <= glyphId && glyphId <= rangeSpan[i].EndGlyphId)
							return rangeSpan[i].StartCoverageIndex + glyphId - rangeSpan[i].StartGlyphId;

						break;
					}
				}

				return (std::numeric_limits<size_t>::max)();
			}
		};
	};

	struct ClassDefTable {
		struct Format1ClassArray {
			struct FormatHeader {
				BE<uint16_t> FormatId;
				BE<uint16_t> StartGlyphId;
				BE<uint16_t> GlyphCount;
			};

			FormatHeader Header;
			BE<uint16_t> ClassValueArray[1];
		};

		struct Format2ClassRanges {
			struct FormatHeader {
				BE<uint16_t> FormatId;
				BE<uint16_t> ClassRangeCount;
			};

			struct ClassRangeRecord {
				BE<uint16_t> StartGlyphId;
				BE<uint16_t> EndGlyphId;
				BE<uint16_t> Class;
			};

			FormatHeader Header;
			ClassRangeRecord ClassValueArray[1];
		};

		union {
			BE<uint16_t> FormatId;
			Format1ClassArray Format1;
			Format2ClassRanges Format2;
		};

		class View {
			union {
				const ClassDefTable* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof FormatId)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				switch (*obj->FormatId) {
					case 1:
						if (data.size_bytes() < sizeof(Format1ClassArray::FormatHeader) + sizeof(BE<uint16_t>) * (*obj->Format1.Header.GlyphCount))
							return;
						break;

					case 2:
						if (data.size_bytes() < sizeof(Format2ClassRanges::FormatHeader) + sizeof(Format2ClassRanges::ClassRangeRecord) * (*obj->Format2.Header.ClassRangeCount))
							return;
						break;

					default:
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::map<uint16_t, std::set<uint16_t>> ClassToGlyphMap() const {
				std::map<uint16_t, std::set<uint16_t>> res;
				switch (m_obj->FormatId) {
					case 1:
					{
						const auto startId = *m_obj->Format1.Header.StartGlyphId;
						const auto count = *m_obj->Format1.Header.GlyphCount;
						for (auto i = 0; i < count; i++)
							res[*m_obj->Format1.ClassValueArray[i]].insert(startId + i);
						break;
					}

					case 2:
					{
						for (const auto& range : std::span(m_obj->Format2.ClassValueArray, m_obj->Format2.Header.ClassRangeCount)) {
							auto& target = res[*range.Class];
							for (auto i = *range.StartGlyphId, i_ = *range.EndGlyphId; i <= i_; i++)
								target.insert(i);
						}
						break;
					}
				}
				return res;
			}

			std::map<uint16_t, uint16_t> GlyphToClassMap() const {
				std::map<uint16_t, uint16_t> res;
				switch (m_obj->FormatId) {
					case 1:
					{
						const auto startId = *m_obj->Format1.Header.StartGlyphId;
						const auto count = *m_obj->Format1.Header.GlyphCount;
						for (auto i = 0; i < count; i++)
							res[startId + i] = *m_obj->Format1.ClassValueArray[i];
						break;
					}

					case 2:
					{
						for (const auto& range : std::span(m_obj->Format2.ClassValueArray, m_obj->Format2.Header.ClassRangeCount)) {
							const auto classValue = *range.Class;
							for (auto i = *range.StartGlyphId, i_ = *range.EndGlyphId; i <= i_; i++)
								res[i] = classValue;
						}
						break;
					}
				}
				return res;
			}

			uint16_t GetClass(uint16_t glyphId) const {
				switch (m_obj->FormatId) {
					case 1:
					{
						const auto startId = *m_obj->Format1.Header.StartGlyphId;
						if (startId <= glyphId && glyphId < startId + *m_obj->Format1.Header.GlyphCount)
							return m_obj->Format1.ClassValueArray[glyphId - startId];

						return 0;
					}

					case 2:
					{
						const auto rangeSpan = std::span(m_obj->Format2.ClassValueArray, m_obj->Format2.Header.ClassRangeCount);
						const auto bec = Format2ClassRanges::ClassRangeRecord{ .EndGlyphId = glyphId };
						const auto i = std::upper_bound(rangeSpan.begin(), rangeSpan.end(), bec, [](const Format2ClassRanges::ClassRangeRecord& l, const Format2ClassRanges::ClassRangeRecord& r) { return *l.EndGlyphId < *r.EndGlyphId; }) - rangeSpan.begin() - 1;
						if (i >= 0 && rangeSpan[i].StartGlyphId <= glyphId && glyphId <= rangeSpan[i].EndGlyphId)
							return rangeSpan[i].Class;

						return 0;
					}
				}

				return 0;
			}
		};
	};

	struct Head {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/head
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6head.html

		static constexpr TagStruct DirectoryTableTag{ { 'h', 'e', 'a', 'd' } };
		static constexpr uint32_t MagicNumberValue = 0x5F0F3CF5;

		struct HeadFlags {
			uint16_t BaselineForFontAtZeroY : 1;
			uint16_t LeftSideBearingAtZeroX : 1;
			uint16_t InstructionsDependOnPointSize : 1;
			uint16_t ForcePpemsInteger : 1;

			uint16_t InstructionsAlterAdvanceWidth : 1;
			uint16_t VerticalLayout : 1;
			uint16_t Reserved6 : 1;
			uint16_t RequiresLayoutForCorrectLinguisticRendering : 1;

			uint16_t IsAatFont : 1;
			uint16_t ContainsRtlGlyph : 1;
			uint16_t ContainsIndicStyleRearrangementEffects : 1;
			uint16_t Lossless : 1;

			uint16_t ProduceCompatibleMetrics : 1;
			uint16_t OptimizedForClearType : 1;
			uint16_t IsLastResortFont : 1;
			uint16_t Reserved15 : 1;
		};
		static_assert(sizeof HeadFlags == 2);

		struct MacStyleFlags {
			uint16_t Bold : 1;
			uint16_t Italic : 1;
			uint16_t Underline : 1;
			uint16_t Outline : 1;
			uint16_t Shadow : 1;
			uint16_t Condensed : 1;
			uint16_t Extended : 1;
			uint16_t Reserved : 9;
		};
		static_assert(sizeof MacStyleFlags == 2);

		Fixed Version;
		Fixed FontRevision;
		BE<uint32_t> ChecksumAdjustment;
		BE<uint32_t> MagicNumber;
		BE<HeadFlags> Flags;
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

		class View {
			union {
				const Head* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (sizeof(*m_obj) > data.size_bytes())
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (obj->Version.Major != 1)
					return;
				if (obj->MagicNumber != MagicNumberValue)
					return;

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}
		};
	};

	struct Name {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/name
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6name.html

		static constexpr TagStruct DirectoryTableTag{ { 'n', 'a', 'm', 'e' } };

		enum class NameId : uint16_t {
			CopyrightNotice = 0,
			FamilyName = 1,
			SubfamilyName = 2,
			UniqueId = 3,
			FullFontName = 4,
			VersionString = 5,
			PostScriptName = 6,
			Trademark = 7,
			Manufacturer = 8,
			Designer = 9,
			Description = 10,
			UrlVendor = 11,
			UrlDesigner = 12,
			LicenseDescription = 13,
			LicenseInfoUrl = 14,
			TypographicFamilyName = 16,
			TypographicSubfamilyName = 17,
			CompatibleFullMac = 18,
			SampleText = 19,
			PoscSriptCidFindFontName = 20,
			WwsFamilyName = 21,
			WwsSubfamilyName = 22,
			LightBackgroundPalette = 23,
			DarkBackgroundPalette = 24,
			VariationPostScriptNamePrefix = 25,
		};

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
			BE<NameId> NameId;
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

		class View {
			union {
				const Name* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length) : m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof NameHeader)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::span<const NameRecord> RecordSpan() const {
				return { m_obj->Record, *m_obj->Header.Count };
			}

			uint16_t LanguageCount() const {
				return *m_obj->Header.Version >= 1 ? **reinterpret_cast<const BE<uint16_t>*>(&m_obj->Record[*m_obj->Header.Count]) : 0;
			}

			std::span<const LanguageRecord> LanguageSpan() const {
				if (*m_obj->Header.Version == 0)
					return {};
				return { reinterpret_cast<const LanguageRecord*>(reinterpret_cast<const char*>(&m_obj->Record[*m_obj->Header.Count]) + 2), LanguageCount() };
			}

			const LanguageRecord& Language(size_t i) {
				return reinterpret_cast<const LanguageRecord*>(reinterpret_cast<const char*>(&m_obj->Record[*m_obj->Header.Count]) + 2)[i];
			}

			template<typename TUnicodeString = std::string>
			TUnicodeString GetUnicodeName(uint16_t preferredLanguageId, NameId nameId) const {
				const auto recordSpans = RecordSpan();
				for (const auto usePreferredLanguageId : { true, false }) {
					for (const auto& record : recordSpans) {
						if (record.LanguageId != preferredLanguageId && usePreferredLanguageId)
							continue;

						if (record.NameId != nameId)
							continue;

						const auto offsetStart = static_cast<size_t>(*m_obj->Header.StorageOffset) + *record.StringOffset;
						const auto offsetEnd = offsetStart + *record.Length;
						if (offsetEnd > m_length)
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
							const auto pString = &m_bytes[offsetStart];
							const auto pStringEnd = &m_bytes[offsetEnd];
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

							return Unicode::Convert<TUnicodeString>(res);
						}

					decode_utf16be:
						{
							const auto pString = reinterpret_cast<const BE<char16_t>*>(&m_bytes[offsetStart]);
							const auto pStringEnd = reinterpret_cast<const BE<char16_t>*>(&m_bytes[offsetEnd]);
							std::u16string u16;
							u16.reserve(pStringEnd - pString);
							for (auto x = pString; x < pStringEnd; x++)
								u16.push_back(*x);

							return Unicode::Convert<TUnicodeString>(u16);
						}
					}
				}
				return {};
			}

			template<typename TUnicodeString = std::string>
			TUnicodeString GetPreferredFamilyName(uint16_t preferredLanguageId) const {
				auto r = GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::TypographicFamilyName);
				if (!r.empty())
					return r;
				return GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::FamilyName);
			}

			template<typename TUnicodeString = std::string>
			TUnicodeString GetPreferredSubfamilyName(uint16_t preferredLanguageId) const {
				auto r = GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::TypographicSubfamilyName);
				if (!r.empty())
					return r;
				return GetUnicodeName<TUnicodeString>(preferredLanguageId, NameId::SubfamilyName);
			}
		};
	};

	struct Cmap {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/cmap
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6cmap.html

		static constexpr TagStruct DirectoryTableTag{ { 'c', 'm', 'a', 'p' } };

		struct CmapHeader {
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

			struct MapGroup {
				BE<uint32_t> StartCharCode;
				BE<uint32_t> EndCharCode;
				BE<uint32_t> GlyphId;
			};

			class IFormatView {
			public:
				virtual void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const = 0;

				virtual operator bool() const = 0;
			};

			class ValidButUnsupportedFormatView : public IFormatView {
			public:
				void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const {}
				operator bool() const override { return true; }
			};

			struct Format0 {
				static constexpr uint16_t FormatId_Value = 0;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
				};

				FormatHeader Header;
				uint8_t GlyphIdArray[256];

				class View : public IFormatView {
					union {
						const Format0* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (data.size_bytes() < sizeof Format0)
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					uint16_t CharToGlyph(uint32_t c) const {
						return c >= 256 ? 0 : m_obj->GlyphIdArray[c];
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (char32_t i = 0; i < 256; i++)
							if (m_obj->GlyphIdArray[i])
								result[m_obj->GlyphIdArray[i]].insert(i);
					}
				};
			};

			struct Format2 {
				static constexpr uint16_t FormatId_Value = 2;

				struct FormatHeader {
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

				FormatHeader Header;
				SubHeader SubHeaders[1];

				class View : public IFormatView {
					union {
						const Format2* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					uint16_t CharToGlyph(uint32_t c) const {
						if (c >= 0x10000)
							return 0;

						const auto& subHeader = m_obj->SubHeaders[m_obj->Header.SubHeaderKeys[c >> 8] / sizeof SubHeader];
						if (reinterpret_cast<const char*>(&subHeader) + sizeof(subHeader) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						c = c & 0xFF;
						if (c < *subHeader.FirstCode || c >= static_cast<uint32_t>(*subHeader.FirstCode + *subHeader.EntryCount))
							return 0;

						const auto glyphArray = reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(&subHeader.IdRangeOffset) + sizeof subHeader.IdRangeOffset + subHeader.IdRangeOffset);
						const auto pGlyphIndex = &glyphArray[c - subHeader.FirstCode];
						if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						c = **pGlyphIndex;
						return c == 0 ? 0 : (c + subHeader.IdDelta) & 0xFFFF;
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (char32_t baseChar = 0; baseChar < 0x10000; baseChar += 0x100) {
							const auto& subHeader = m_obj->SubHeaders[m_obj->Header.SubHeaderKeys[baseChar >> 8] / sizeof SubHeader];
							if (reinterpret_cast<const char*>(&subHeader) + sizeof(subHeader) > m_bytes + *m_obj->Header.Length)
								continue;  // overflow

							const auto glyphArray = reinterpret_cast<const BE<uint16_t>*>(reinterpret_cast<const char*>(&subHeader.IdRangeOffset) + sizeof subHeader.IdRangeOffset + subHeader.IdRangeOffset);
							for (char32_t c = baseChar + subHeader.FirstCode, c2_ = c + subHeader.EntryCount; c < c2_; c++) {
								const auto pGlyphIndex = &glyphArray[baseChar - subHeader.FirstCode];
								if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
									continue; // overflow

								const auto glyphIndex = **pGlyphIndex;
								if (glyphIndex)
									result[(baseChar + subHeader.IdDelta) & 0xFFFF].insert(c);
							}
						}
					}
				};
			};

			struct Format4 {
				static constexpr uint16_t FormatId_Value = 4;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> SegCountX2;
					BE<uint16_t> SearchRange;
					BE<uint16_t> EntrySelector;
					BE<uint16_t> RangeShift;

					const size_t SegCount() const {
						return *SegCountX2 / 2;
					}
				};

				FormatHeader Header;
				BE<uint16_t> Data[1];

				class View : public IFormatView {
					union {
						const Format4* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (sizeof Header + 4 + *obj->Header.SegCountX2 * 3 > data.size_bytes())
							return;

						if (reinterpret_cast<const char*>(&obj->Data[1 + obj->Header.SegCount() * 4]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					const BE<uint16_t>& EndCode(size_t i) const { return m_obj->Data[0 + m_obj->Header.SegCount() * 0 + i]; }
					const BE<uint16_t>& StartCode(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 1 + i]; }
					const BE<uint16_t>& IdDelta(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 2 + i]; }
					const BE<uint16_t>& IdRangeOffset(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 3 + i]; }
					const BE<uint16_t>& GlyphIndex(size_t i) const { return m_obj->Data[1 + m_obj->Header.SegCount() * 4 + i]; }

					std::span<const BE<uint16_t>> EndCodeSpan() const { return { &EndCode(0), m_obj->Header.SegCount() }; }
					std::span<const BE<uint16_t>> StartCodeSpan() const { return { &StartCode(0), m_obj->Header.SegCount() }; }
					std::span<const BE<uint16_t>> IdDeltaSpan() const { return { &IdDelta(0), m_obj->Header.SegCount() }; }
					std::span<const BE<uint16_t>> IdRangeOffsetSpan() const { return { &IdRangeOffset(0), m_obj->Header.SegCount() }; }
					std::span<const BE<uint16_t>> GlyphIndexSpan() const { return { &GlyphIndex(0), static_cast<size_t>((m_bytes + m_length - reinterpret_cast<const char*>(&GlyphIndex(0))) / 2) }; }

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
						if (reinterpret_cast<const char*>(pIdRangeOffset) + sizeof(*pIdRangeOffset) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						const auto idRangeOffset = **pIdRangeOffset;
						const auto idDelta = *IdDelta(i);
						if (idRangeOffset == 0)
							return (idDelta + c) & 0xFFFF;

						const auto pGlyphIndex = &pIdRangeOffset[idRangeOffset / 2 + c - startCode];
						if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
							return 0; // overflow

						const auto glyphIndex = **pGlyphIndex;
						return glyphIndex == 0 ? 0 : (idDelta + glyphIndex) & 0xFFFF;
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						const auto startCodes = StartCodeSpan();
						const auto endCodes = EndCodeSpan();
						const auto idDeltas = IdDeltaSpan();
						const auto idRangeOffsets = IdRangeOffsetSpan();
						const auto glyphIndices = GlyphIndexSpan();

						for (size_t i = 0, i_ = m_obj->Header.SegCount(); i < i_; i++) {
							const auto startCode = static_cast<char32_t>(*startCodes[i]);
							const auto endCode = static_cast<char32_t>(*endCodes[i]);
							const auto idDelta = static_cast<size_t>(*idDeltas[i]);
							const auto idRangeOffset = static_cast<size_t>(*idRangeOffsets[i]);

							if (idRangeOffset == 0) {
								for (auto c = startCode; c <= endCode; c++) {
									const auto glyphId = (idDelta + c) & 0xFFFF;
									result[glyphId].insert(c);
								}

							} else {
								const auto pIdRangeOffset = &IdRangeOffset(i);

								if (reinterpret_cast<const char*>(pIdRangeOffset) + sizeof(*pIdRangeOffset) > m_bytes + *m_obj->Header.Length)
									continue; // overflow

								for (auto c = startCode; c <= endCode; c++) {
									const auto pGlyphIndex = &pIdRangeOffset[idRangeOffset / 2 + c - startCode];
									if (reinterpret_cast<const char*>(pGlyphIndex) + sizeof(*pGlyphIndex) > m_bytes + *m_obj->Header.Length)
										break; // overflow

									const auto glyphIndex = **pGlyphIndex;
									if (!glyphIndex)
										continue;

									const auto glyphId = (idDelta + glyphIndex) & 0xFFFF;
									result[glyphId].insert(c);
								}
							}
						}
					}
				};
			};

			struct Format6 {
				static constexpr uint16_t FormatId_Value = 6;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Length;
					BE<uint16_t> Language;  // Only used for Macintosh platforms
					BE<uint16_t> FirstCode;
					BE<uint16_t> EntryCount;
				};

				FormatHeader Header;
				BE<uint16_t> GlyphId[1];

				class View : public IFormatView {
					union {
						const Format6* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->GlyphId[*obj->Header.EntryCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					std::span<const BE<uint16_t>> GlyphIdSpan() const { return { m_obj->GlyphId, *m_obj->Header.EntryCount }; }

					uint16_t CharToGlyph(uint32_t c) const {
						if (c < *m_obj->Header.FirstCode || c >= static_cast<uint32_t>(*m_obj->Header.FirstCode + *m_obj->Header.EntryCount))
							return 0;

						return m_obj->GlyphId[c - m_obj->Header.FirstCode];
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (char32_t c = *m_obj->Header.FirstCode, c_ = c + *m_obj->Header.EntryCount; c < c_; c++)
							if (const auto glyphId = *m_obj->GlyphId[c])
								result[glyphId].insert(c);
					}
				};
			};

			struct Format8 {
				static constexpr uint16_t FormatId_Value = 8;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					uint8_t Is32[8192];
					BE<uint32_t> GroupCount;
				};

				FormatHeader Header;
				MapGroup Group[1];

				class View : public IFormatView {
					union {
						const Format8* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->Group[*obj->Header.GroupCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					std::span<const MapGroup> GroupSpan() const { return { const_cast<MapGroup*>(m_obj->Group), static_cast<size_t>(*m_obj->Header.GroupCount) }; }

					uint16_t CharToGlyph(uint32_t c) const {
						MapGroup tmp;
						tmp.EndCharCode = c;
						const auto& group = std::upper_bound(GroupSpan().begin(), GroupSpan().end(), tmp, [](const MapGroup& l, const MapGroup& r) { return *l.EndCharCode < *r.EndCharCode; })[-1];
						if (&group < m_obj->Group || c < *group.StartCharCode || c > *group.EndCharCode)
							return 0;

						return group.GlyphId + c - group.StartCharCode;
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (const auto& group : GroupSpan())
							for (char32_t c = *group.StartCharCode, c_ = *group.EndCharCode; c <= c_; c++)
								result[group.GlyphId + c - group.StartCharCode].insert(c);
					}
				};
			};

			struct Format10 {
				static constexpr uint16_t FormatId_Value = 10;

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					BE<uint32_t> FirstCode;
					BE<uint32_t> EntryCount;
				};

				FormatHeader Header;
				BE<uint16_t> GlyphId[1];

				class View : public IFormatView {
					union {
						const Format10* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (*obj->Header.FormatId != FormatId_Value || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->GlyphId[*obj->Header.EntryCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					std::span<const BE<uint16_t>> GlyphIdSpan() const { return { m_obj->GlyphId, *m_obj->Header.EntryCount }; }

					uint16_t CharToGlyph(uint32_t c) const {
						if (c < m_obj->Header.FirstCode || c >= m_obj->Header.FirstCode + m_obj->Header.EntryCount)
							return 0;

						return m_obj->GlyphId[c];
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						for (char32_t c = *m_obj->Header.FirstCode, c_ = c + *m_obj->Header.EntryCount; c < c_; c++)
							if (const auto glyphId = *m_obj->GlyphId[c])
								result[glyphId].insert(c);
					}
				};
			};

			struct Format12And13 {
				static constexpr uint16_t FormatId_Values[]{ 12, 13 };

				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> Reserved;
					BE<uint32_t> Length;
					BE<uint32_t> Language;  // Only used for Macintosh platforms
					BE<uint32_t> GroupCount;
				};

				FormatHeader Header;
				MapGroup MapGroups[1];

				class View : public IFormatView {
					union {
						const Format12And13* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if ((*obj->Header.FormatId != FormatId_Values[0] && *obj->Header.FormatId != FormatId_Values[1]) || data.size_bytes() < *obj->Header.Length)
							return;

						if (reinterpret_cast<const char*>(&obj->MapGroups[*obj->Header.GroupCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
							return;

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const override {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					std::span<const MapGroup> MapGroupSpan() const { return { const_cast<MapGroup*>(m_obj->MapGroups), *m_obj->Header.GroupCount }; }

					uint16_t CharToGlyph(uint32_t c) const {
						MapGroup tmp;
						tmp.EndCharCode = c;
						const auto& group = std::upper_bound(MapGroupSpan().begin(), MapGroupSpan().end(), tmp, [](const MapGroup& l, const MapGroup& r) { return *l.EndCharCode < *r.EndCharCode; })[-1];
						if (&group < m_obj->MapGroups || c < *group.StartCharCode || c > *group.EndCharCode)
							return 0;

						if (*m_obj->Header.FormatId == 12)
							return group.GlyphId + c - group.StartCharCode;
						else
							return group.GlyphId;
					}

					void GetGlyphToCharMap(std::vector<std::set<char32_t>>& result) const override {
						if (*m_obj->Header.FormatId == 12) {
							for (const auto& group : MapGroupSpan())
								for (char32_t c = *group.StartCharCode, c_ = *group.EndCharCode; c <= c_; c++)
									result[group.GlyphId + c - group.StartCharCode].insert(c);
						} else {
							for (const auto& group : MapGroupSpan())
								for (char32_t c = *group.StartCharCode, c_ = *group.EndCharCode; c <= c_; c++)
									result[group.GlyphId].insert(c);
						}
					}
				};
			};

			static std::unique_ptr<IFormatView> GetFormatView(const void* pData, size_t length) {
				if (length < sizeof FormatId)
					return nullptr;
				switch (*static_cast<const Format*>(pData)->FormatId) {
					case 0: return TryMakeUniqueFormatView<Format::Format0::View>(pData, length);
					case 2: return TryMakeUniqueFormatView<Format::Format2::View>(pData, length);
					case 4: return TryMakeUniqueFormatView<Format::Format4::View>(pData, length);
					case 6: return TryMakeUniqueFormatView<Format::Format6::View>(pData, length);
					case 8: return TryMakeUniqueFormatView<Format::Format8::View>(pData, length);
					case 10: return TryMakeUniqueFormatView<Format::Format10::View>(pData, length);
					case 12:
					case 13: return TryMakeUniqueFormatView<Format::Format12And13::View>(pData, length);
					default: return std::make_unique<ValidButUnsupportedFormatView>();
				}
			}

			template<typename T>
			static std::unique_ptr<IFormatView> GetFormatView(std::span<T> data) { return GetFormatView(&data[0], data.size_bytes()); }

		private:
			template<typename TFormatView>
			static std::unique_ptr<IFormatView> TryMakeUniqueFormatView(const void* pData, size_t length) {
				std::unique_ptr<IFormatView> p = std::make_unique<TFormatView>(pData, length);
				if (p && *p)
					return p;
				return nullptr;
			}
		};

		CmapHeader Header;
		EncodingRecord EncodingRecords[1];

		class View {
			union {
				const Cmap* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof(*m_obj))
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof(OffsetTableStruct) + sizeof(EncodingRecord) * obj->Header.SubtableCount)
					return;

				for (size_t i = 0, i_ = *obj->Header.SubtableCount; i < i_; ++i) {
					const auto& encRec = obj->EncodingRecords[i];
					if (encRec.SubtableOffset + sizeof uint16_t > data.size_bytes())
						return;

					const auto pFormatView = Format::GetFormatView(std::span(reinterpret_cast<const char*>(obj), data.size_bytes()).subspan(*encRec.SubtableOffset));
					if (!*pFormatView)
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::vector<std::set<char32_t>> GetGlyphToCharMap() const {
				std::vector<std::set<char32_t>> result;
				result.resize(65536);

				for (size_t i = 0, i_ = *m_obj->Header.SubtableCount; i < i_; ++i) {
					const auto& encRec = m_obj->EncodingRecords[i];
					if (encRec.Platform == PlatformId::Unicode)
						void();
					else if (encRec.Platform == PlatformId::Windows && encRec.WindowsEncoding == WindowsPlatformEncodingId::UnicodeBmp)
						void();
					else if (encRec.Platform == PlatformId::Windows && encRec.WindowsEncoding == WindowsPlatformEncodingId::UnicodeFullRepertoire)
						void();
					else
						continue;

					Format::GetFormatView(std::span(m_bytes, m_length).subspan(*encRec.SubtableOffset))->GetGlyphToCharMap(result);
				}

				return result;
			}
		};
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

			Header Header;
			Pair Pairs[1];

			class View {
				union {
					const Format0* m_obj;
					const char* m_bytes;
				};
				size_t m_length;

			public:
				View() : m_obj(nullptr), m_length(0) {}
				View(std::nullptr_t) : View() {}
				View(decltype(m_obj) pObject, size_t length)
					: m_obj(pObject), m_length(length) {}
				View(View&&) = default;
				View(const View&) = default;
				View& operator=(View&&) = default;
				View& operator=(const View&) = default;
				View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
				View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
				template<typename T>
				View(std::span<T> data) : View() {
					if (sizeof Header > data.size_bytes())
						return;

					const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

					if (reinterpret_cast<const char*>(&obj->Pairs[obj->Header.PairCount]) > reinterpret_cast<const char*>(obj) + data.size_bytes())
						return;

					m_obj = obj;
					m_length = data.size_bytes();
				}

				operator bool() const {
					return !!m_obj;
				}

				decltype(m_obj) operator*() const {
					return m_obj;
				}

				decltype(m_obj) operator->() const {
					return m_obj;
				}

				void Parse(
					std::map<std::pair<char32_t, char32_t>, int>& result,
					const std::vector<std::set<char32_t>>& glyphToCharMap,
					bool cumulative
				) const {
					for (auto pPair = m_obj->Pairs, pPair_ = m_obj->Pairs + m_obj->Header.PairCount; pPair < pPair_; pPair++) {
						for (const auto l : glyphToCharMap[*pPair->Left]) {
							for (const auto r : glyphToCharMap[*pPair->Right]) {
								auto& target = result[std::make_pair(l, r)];
								if (cumulative)
									target += *pPair->Value;
								else
									target = *pPair->Value;
							}
						}
					}
				}
			};
		};

		struct Version0 {
			struct KernHeader {
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

			KernHeader Header;
			SubtableHeader FirstSubtable;

			class View {
				union {
					const Version0* m_obj;
					const char* m_bytes;
				};
				size_t m_length;

			public:
				View() : m_obj(nullptr), m_length(0) {}
				View(std::nullptr_t) : View() {}
				View(decltype(m_obj) pObject, size_t length)
					: m_obj(pObject), m_length(length) {}
				View(View&&) = default;
				View(const View&) = default;
				View& operator=(View&&) = default;
				View& operator=(const View&) = default;
				View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
				View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
				template<typename T>
				View(std::span<T> data) : View() {
					if (data.size_bytes() < sizeof KernHeader)
						return;

					const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

					if (obj->Header.Version != 0)
						return;

					m_obj = obj;
					m_length = data.size_bytes();
				}

				operator bool() const {
					return !!m_obj;
				}

				decltype(m_obj) operator*() const {
					return m_obj;
				}

				decltype(m_obj) operator->() const {
					return m_obj;
				}

				void Parse(
					std::map<std::pair<char32_t, char32_t>, int>& result,
					const std::vector<std::set<char32_t>>& glyphToCharMap
				) {
					std::span<const char> data{ reinterpret_cast<const char*>(&m_obj->FirstSubtable), m_length - sizeof KernHeader };

					for (size_t i = 0; i < m_obj->Header.SubtableCount; ++i) {
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
									if (Format0::View view(formatData); view)
										view.Parse(result, glyphToCharMap, !coverage.Override);
									break;
							}
						}

						data = data.subspan(kernSubtableHeader.Length);
					}
				}
			};
		};

		struct Version1 {
			struct KernHeader {
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

			KernHeader Header;
			SubtableHeader FirstSubtable;

			class View {
				union {
					const Version1* m_obj;
					const char* m_bytes;
				};
				size_t m_length;

			public:
				View() : m_obj(nullptr), m_length(0) {}
				View(std::nullptr_t) : View() {}
				View(decltype(m_obj) pObject, size_t length)
					: m_obj(pObject), m_length(length) {}
				View(View&&) = default;
				View(const View&) = default;
				View& operator=(View&&) = default;
				View& operator=(const View&) = default;
				View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
				View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
				template<typename T>
				View(std::span<T> data) : View() {
					if (data.size_bytes() < sizeof KernHeader)
						return;

					const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

					if (obj->Header.Version != 0x00010000)
						return;

					m_obj = obj;
					m_length = data.size_bytes();
				}

				operator bool() const {
					return !!m_obj;
				}

				decltype(m_obj) operator*() const {
					return m_obj;
				}

				decltype(m_obj) operator->() const {
					return m_obj;
				}

				void Parse(
					std::map<std::pair<char32_t, char32_t>, int>& result,
					const std::vector<std::set<char32_t>>& glyphToCharMap
				) {
					// Untested

					std::span<const char> data{ reinterpret_cast<const char*>(&m_obj->FirstSubtable), m_length - sizeof KernHeader };

					for (size_t i = 0; i < m_obj->Header.SubtableCount; ++i) {
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
									if (Format0::View view(formatData); view)
										view.Parse(result, glyphToCharMap, false);
									break;

								default:
									__debugbreak();
							}
						}

						data = data.subspan(kernSubtableHeader.Length);
					}
				}
			};
		};

		union {
			Version0 V0;
			Version1 V1;
		};

		class View {
			union {
				const Kern* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof Version0::KernHeader)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::map<std::pair<char32_t, char32_t>, int> Parse(const std::vector<std::set<char32_t>>& glyphToCharMap) const {
				std::map<std::pair<char32_t, char32_t>, int> result;

				switch (*m_obj->V0.Header.Version) {
					case 0:
						if (Version0::View v(m_bytes, m_length); v)
							v.Parse(result, glyphToCharMap);
						break;

					case 1:
						if (Version1::View v(m_bytes, m_length); v)
							v.Parse(result, glyphToCharMap);
						break;
				}

				return result;
			}
		};
	};

	struct Gpos {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/gpos

		static constexpr TagStruct DirectoryTableTag{ { 'G', 'P', 'O', 'S' } };

		struct GposHeaderV1_0 {
			Fixed Version;
			BE<uint16_t> ScriptListOffset;
			BE<uint16_t> FeatureListOffset;
			BE<uint16_t> LookupListOffset;
		};

		struct GposHeaderV1_1 : GposHeaderV1_0 {
			BE<uint32_t> FeatureVariationsOffset;
		};

		union ValueFormatFlags {
			uint16_t Value;
			struct {
				uint16_t PlacementX : 1;
				uint16_t PlacementY : 1;
				uint16_t AdvanceX : 1;
				uint16_t AdvanceY : 1;
				uint16_t PlaDeviceOffsetX : 1;
				uint16_t PlaDeviceOffsetY : 1;
				uint16_t AdvDeviceOffsetX : 1;
				uint16_t AdvDeviceOffsetY : 1;
				uint16_t Reserved : 8;
			};
		};
		static_assert(sizeof ValueFormatFlags == 2);

		union PairAdjustmentPositioningSubtable {
			struct Format1 {
				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> CoverageOffset;
					BE<ValueFormatFlags> ValueFormat1;
					BE<ValueFormatFlags> ValueFormat2;
					BE<uint16_t> PairSetCount;
				};

				struct PairSet {
					BE<uint16_t> Count;
					BE<uint16_t> Records[1];

					class View {
						union {
							const PairSet* m_obj;
							const char* m_bytes;
						};
						size_t m_length;
						ValueFormatFlags m_format1;
						ValueFormatFlags m_format2;
						uint32_t m_bit;
						size_t m_valueCountPerPairValueRecord;

					public:
						View() : m_obj(nullptr), m_length(0), m_format1{ 0 }, m_format2{ 0 }, m_bit(0), m_valueCountPerPairValueRecord(0) {}
						View(std::nullptr_t) : View() {}
						View(decltype(m_obj) pObject, size_t length, ValueFormatFlags format1, ValueFormatFlags format2, uint32_t bit, size_t valueCountPerPairValueRecord)
							: m_obj(pObject), m_length(length), m_format1(format1), m_format2(format2), m_bit(bit), m_valueCountPerPairValueRecord(valueCountPerPairValueRecord) {}
						View(View&&) = default;
						View(const View&) = default;
						View& operator=(View&&) = default;
						View& operator=(const View&) = default;
						View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
						View(const void* pData, size_t length, ValueFormatFlags format1, ValueFormatFlags format2) : View(std::span(reinterpret_cast<const char*>(pData), length), format1, format2) {}
						template<typename T>
						View(std::span<T> data, ValueFormatFlags format1, ValueFormatFlags format2) : View() {
							if (data.size_bytes() < 2)
								return;

							const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

							const auto bit = (format2.Value << 16) | format1.Value;
							const auto valueCountPerPairValueRecord = static_cast<size_t>(1 + std::popcount<uint32_t>(bit));

							if (data.size_bytes() < static_cast<size_t>(2) + 2 * valueCountPerPairValueRecord * (*obj->Count))
								return;

							m_obj = obj;
							m_length = data.size_bytes();
							m_format1 = format1;
							m_format2 = format2;
							m_bit = bit;
							m_valueCountPerPairValueRecord = valueCountPerPairValueRecord;
						}

						operator bool() const {
							return !!m_obj;
						}

						decltype(m_obj) operator*() const {
							return m_obj;
						}

						decltype(m_obj) operator->() const {
							return m_obj;
						}

						const BE<uint16_t>* GetPairValueRecord(size_t index) const {
							return &m_obj->Records[m_valueCountPerPairValueRecord * index];
						}

						uint16_t GetSecondGlyph(size_t index) const {
							return **GetPairValueRecord(index);
						}

						uint16_t GetValueRecord1(size_t index, ValueFormatFlags desiredRecord) const {
							if (!(m_format1.Value & desiredRecord.Value))
								return 0;
							auto bit = m_bit;
							auto pRecord = GetPairValueRecord(index);
							for (auto i = static_cast<uint32_t>(desiredRecord.Value); i && bit; i >>= 1, bit >>= 1) {
								if (bit & 1)
									pRecord++;
							}
							return *pRecord;
						}

						uint16_t GetValueRecord2(size_t index, ValueFormatFlags desiredRecord) const {
							if (!(m_format2.Value & desiredRecord.Value))
								return 0;
							auto bit = m_bit;
							auto pRecord = GetPairValueRecord(index);
							for (auto i = static_cast<uint32_t>(desiredRecord.Value) << 16; i && bit; i >>= 1, bit >>= 1) {
								if (bit & 1)
									pRecord++;
							}
							return *pRecord;
						}
					};
				};

				FormatHeader Header;
				BE<uint16_t> PairSetOffsets[1];

				class View {
					union {
						const Format1* m_obj;
						const char* m_bytes;
					};
					size_t m_length;

				public:
					View() : m_obj(nullptr), m_length(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length)
						: m_obj(pObject), m_length(length) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < sizeof FormatHeader)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						if (obj->Header.FormatId != 1)
							return;

						if (data.size_bytes() < sizeof FormatHeader + static_cast<size_t>(2) * (*obj->Header.PairSetCount))
							return;

						if (CoverageTable::View coverageTable(reinterpret_cast<const char*>(obj) + *obj->Header.CoverageOffset, data.size_bytes() - *obj->Header.CoverageOffset); !coverageTable)
							return;

						for (size_t i = 0, i_ = *obj->Header.PairSetCount; i < i_; i++) {
							const auto off = static_cast<size_t>(*obj->PairSetOffsets[i]);
							if (data.size_bytes() < off + 2)
								return;

							const auto pPairSet = reinterpret_cast<const PairSet*>(reinterpret_cast<const char*>(obj) + off);
							if (data.size_bytes() < off + 2 + static_cast<size_t>(2) * (*pPairSet->Count))
								return;
						}

						m_obj = obj;
						m_length = data.size_bytes();
					}

					operator bool() const {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					std::span<const BE<uint16_t>> PairSetOffsetSpan() const {
						return { m_obj->PairSetOffsets, m_obj->Header.PairSetCount };
					}

					PairSet::View PairSetView(size_t index) const {
						const auto offset = static_cast<size_t>(*m_obj->PairSetOffsets[index]);
						return { reinterpret_cast<const char*>(m_obj) + offset, m_length - offset, m_obj->Header.ValueFormat1, m_obj->Header.ValueFormat2 };
					}

					CoverageTable::View CoverageTableView() const {
						const auto offset = static_cast<size_t>(*m_obj->Header.CoverageOffset);
						return { reinterpret_cast<const char*>(m_obj) + offset, m_length - offset };
					}
				};
			};

			struct Format2 {
				struct FormatHeader {
					BE<uint16_t> FormatId;
					BE<uint16_t> CoverageOffset;
					BE<ValueFormatFlags> ValueFormat1;
					BE<ValueFormatFlags> ValueFormat2;
					BE<uint16_t> ClassDef1Offset;
					BE<uint16_t> ClassDef2Offset;
					BE<uint16_t> Class1Count;
					BE<uint16_t> Class2Count;
				};

				// Note:
				// ClassRecord1 { Class2Record[Class2Count]; }
				// ClassRecord2 { ValueFormat1; ValueFormat2; }

				FormatHeader Header;
				BE<uint16_t> Records[1];

				class View {
					union {
						const Format2* m_obj;
						const char* m_bytes;
					};
					size_t m_length;
					uint32_t m_bit;
					size_t m_valueCountPerPairValueRecord;

				public:
					View() : m_obj(nullptr), m_length(0), m_bit(0), m_valueCountPerPairValueRecord(0) {}
					View(std::nullptr_t) : View() {}
					View(decltype(m_obj) pObject, size_t length, uint32_t bit, size_t valueCountPerPairValueRecord)
						: m_obj(pObject), m_length(length), m_bit(bit), m_valueCountPerPairValueRecord(valueCountPerPairValueRecord) {}
					View(View&&) = default;
					View(const View&) = default;
					View& operator=(View&&) = default;
					View& operator=(const View&) = default;
					View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
					View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
					template<typename T>
					View(std::span<T> data) : View() {
						if (data.size_bytes() < 2)
							return;

						const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

						const auto bit = ((*obj->Header.ValueFormat2).Value << 16) | (*obj->Header.ValueFormat1).Value;
						const auto valueCountPerPairValueRecord = static_cast<size_t>(1 + std::popcount<uint32_t>(bit));

						if (data.size_bytes() < sizeof FormatHeader + sizeof BE<uint16_t> *valueCountPerPairValueRecord * (*obj->Header.Class1Count) * (*obj->Header.Class2Count))
							return;

						if (ClassDefTable::View v(reinterpret_cast<const char*>(obj) + *obj->Header.ClassDef1Offset, data.size_bytes() - *obj->Header.ClassDef1Offset); !v)
							return;
						if (ClassDefTable::View v(reinterpret_cast<const char*>(obj) + *obj->Header.ClassDef2Offset, data.size_bytes() - *obj->Header.ClassDef2Offset); !v)
							return;

						m_obj = obj;
						m_length = data.size_bytes();
						m_bit = bit;
						m_valueCountPerPairValueRecord = valueCountPerPairValueRecord;
					}

					operator bool() const {
						return !!m_obj;
					}

					decltype(m_obj) operator*() const {
						return m_obj;
					}

					decltype(m_obj) operator->() const {
						return m_obj;
					}

					const BE<uint16_t>* GetPairValueRecord(size_t class1, size_t class2) const {
						return &m_obj->Records[m_valueCountPerPairValueRecord * (class1 * *m_obj->Header.Class2Count + class2)];
					}

					uint16_t GetValueRecord1(size_t class1, size_t class2, ValueFormatFlags desiredRecord) const {
						if (!((*m_obj->Header.ValueFormat1).Value & desiredRecord.Value))
							return 0;
						auto bit = m_bit;
						auto pRecord = GetPairValueRecord(class1, class2);
						for (auto i = static_cast<uint32_t>(desiredRecord.Value); i && bit; i >>= 1, bit >>= 1) {
							if (bit & 1)
								pRecord++;
						}
						return **pRecord;
					}

					uint16_t GetValueRecord2(size_t class1, size_t class2, ValueFormatFlags desiredRecord) const {
						if (!((*m_obj->Header.ValueFormat2).Value & desiredRecord.Value))
							return 0;
						auto bit = m_bit;
						auto pRecord = GetPairValueRecord(class1, class2);
						for (auto i = static_cast<uint32_t>(desiredRecord.Value) << 16; i && bit; i >>= 1, bit >>= 1) {
							if (bit & 1)
								pRecord++;
						}
						return **pRecord;
					}

					ClassDefTable::View GetClassTableDefinition1() const {
						return { m_bytes + *m_obj->Header.ClassDef1Offset, m_length - *m_obj->Header.ClassDef1Offset };
					}

					ClassDefTable::View GetClassTableDefinition2() const {
						return { m_bytes + *m_obj->Header.ClassDef2Offset, m_length - *m_obj->Header.ClassDef2Offset };
					}
				};
			};
		};

		union {
			Fixed Version;
			GposHeaderV1_0 HeaderV1_1;
			GposHeaderV1_1 HeaderV1_0;
		};

		class View {
			union {
				const Gpos* m_obj;
				const char* m_bytes;
			};
			size_t m_length;

		public:
			View() : m_obj(nullptr), m_length(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length)
				: m_obj(pObject), m_length(length) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof GposHeaderV1_0)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (obj->Version.Major < 1)
					return;

				if (obj->Version.Major > 1 || (obj->Version.Major == 1 && obj->Version.Minor >= 1)) {
					if (data.size_bytes() < sizeof GposHeaderV1_1)
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::span<const BE<uint16_t>> LookupListOffsets() const {
				const BE<uint16_t>* p = reinterpret_cast<const BE<uint16_t>*>(m_bytes + *m_obj->HeaderV1_0.LookupListOffset);
				return { p + 1, **p };
			}

			std::map<std::pair<char32_t, char32_t>, int> ExtractAdvanceX(const std::vector<std::set<char32_t>>& glyphToCharMap) const {
				std::map<std::pair<char32_t, char32_t>, int> result;

				const auto lookupListOffset = *m_obj->HeaderV1_0.LookupListOffset;
				LookupList::View lookupList(m_bytes + lookupListOffset, m_length - lookupListOffset);
				if (!lookupList)
					return {};

				for (const auto& lookupTableOffset : lookupList.Offsets()) {
					const auto offset = lookupListOffset + *lookupTableOffset;
					LookupTable::View lookupTable(m_bytes + offset, m_length - offset);
					if (!lookupTable)
						continue;
					if (*lookupTable->Header.LookupType != 2)
						continue;  // Not Pair Adjustment Positioning Subtable

					for (size_t subtableIndex = 0, i_ = *lookupTable->Header.SubtableCount; subtableIndex < i_; subtableIndex++) {
						const auto subtableSpan = lookupTable.SubtableSpan(subtableIndex);
						if (PairAdjustmentPositioningSubtable::Format1::View v(subtableSpan); v) {
							if (!(*v->Header.ValueFormat1).AdvanceX && !(*v->Header.ValueFormat2).PlacementX)
								continue;

							const auto coverageTable = v.CoverageTableView();
							if (coverageTable->Header.FormatId == 1) {
								const auto glyphSpan = coverageTable.GlyphSpan();
								for (size_t coverageIndex = 0; coverageIndex < glyphSpan.size(); coverageIndex++) {
									const auto glyph1Id = *glyphSpan[coverageIndex];
									for (const auto c1 : glyphToCharMap[glyph1Id]) {
										const auto pairSetView = v.PairSetView(coverageIndex);
										for (size_t pairIndex = 0, j_ = *pairSetView->Count; pairIndex < j_; pairIndex++) {
											for (const auto c2 : glyphToCharMap[pairSetView.GetSecondGlyph(pairIndex)]) {
												const auto val = static_cast<int16_t>(pairSetView.GetValueRecord1(pairIndex, { .AdvanceX = 1 }))
													+ static_cast<int16_t>(pairSetView.GetValueRecord2(pairIndex, { .PlacementX = 1 }));
												if (val)
													result[std::make_pair(c1, c2)] = val;
											}
										}
									}
								}

							} else if (coverageTable->Header.FormatId == 2) {
								for (const auto& rangeRecord : coverageTable.RangeRecordSpan()) {
									const auto startGlyphId = static_cast<size_t>(*rangeRecord.StartGlyphId);
									const auto endGlyphId = static_cast<size_t>(*rangeRecord.EndGlyphId);
									const auto startCoverageIndex = static_cast<size_t>(*rangeRecord.StartCoverageIndex);
									for (size_t glyphIndex = 0, i_ = endGlyphId - startGlyphId; glyphIndex < i_; glyphIndex++) {
										const auto glyph1Id = startGlyphId + glyphIndex;
										for (const auto c1 : glyphToCharMap[glyph1Id]) {
											const auto pairSetView = v.PairSetView(startCoverageIndex + glyphIndex);
											for (size_t pairIndex = 0, j_ = *pairSetView->Count; pairIndex < j_; pairIndex++) {
												for (const auto c2 : glyphToCharMap[pairSetView.GetSecondGlyph(pairIndex)]) {
													const auto val = static_cast<int16_t>(pairSetView.GetValueRecord1(pairIndex, { .AdvanceX = 1 }))
														+ static_cast<int16_t>(pairSetView.GetValueRecord2(pairIndex, { .PlacementX = 1 }));
													if (val)
														result[std::make_pair(c1, c2)] = val;
												}
											}
										}
									}
								}
							}

						} else if (PairAdjustmentPositioningSubtable::Format2::View v(subtableSpan); v) {
							if (!(*v->Header.ValueFormat1).AdvanceX && !(*v->Header.ValueFormat2).PlacementX)
								continue;

							for (const auto& [class1, glyphs1] : v.GetClassTableDefinition1().ClassToGlyphMap()) {
								if (class1 >= v->Header.Class1Count)
									continue;

								for (const auto& [class2, glyphs2] : v.GetClassTableDefinition2().ClassToGlyphMap()) {
									if (class2 >= v->Header.Class1Count)
										continue;

									const auto val = static_cast<int>(
										static_cast<int16_t>(v.GetValueRecord1(class1, class2, { .AdvanceX = 1 }))
										+ static_cast<int16_t>(v.GetValueRecord2(class1, class2, { .PlacementX = 1 })));
									if (!val)
										continue;

									for (const auto glyph1 : glyphs1) {
										for (const auto c1 : glyphToCharMap[glyph1]) {
											for (const auto glyph2 : glyphs2) {
												for (const auto c2 : glyphToCharMap[glyph2]) {
													result[std::make_pair(c1, c2)] = val;
												}
											}
										}
									}
								}
							}
						}
					};
				}

				return result;
			}
		};
	};

	struct SfntFile {
		// http://formats.kaitai.io/ttf/ttf.svg

		OffsetTableStruct OffsetTable;
		DirectoryTableEntry DirectoryTable[1];

		class View {
			union {
				const SfntFile* m_obj;
				const char* m_bytes;
			};
			size_t m_length;
			size_t m_offsetInCollection;

		public:
			View() : m_obj(nullptr), m_length(0), m_offsetInCollection(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length, size_t offsetInCollection = 0)
				: m_obj(pObject), m_length(length), m_offsetInCollection(offsetInCollection) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length, size_t offsetInCollection = 0) : View(std::span(reinterpret_cast<const char*>(pData), length), offsetInCollection) {}
			template<typename T>
			View(std::span<T> data, size_t offsetInCollection = 0) : View() {
				if (data.size_bytes() < sizeof OffsetTable)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (data.size_bytes() < sizeof OffsetTable + sizeof DirectoryTableEntry * obj->OffsetTable.TableCount)
					return;

				size_t requiredLength = sizeof(OffsetTableStruct) + sizeof(DirectoryTableEntry) * *obj->OffsetTable.TableCount;
				for (size_t i = 0, i_ = *obj->OffsetTable.TableCount; i < i_; i++)
					requiredLength = (std::max)(requiredLength, static_cast<size_t>(*obj->DirectoryTable[i].Offset) + *obj->DirectoryTable[i].Length);
				if (requiredLength > data.size_bytes())
					return;

				m_obj = obj;
				m_length = data.size_bytes();
				m_offsetInCollection = offsetInCollection;
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			std::span<const DirectoryTableEntry> DirectoryTableSpan() const {
				return { m_obj->DirectoryTable, *m_obj->OffsetTable.TableCount };
			}

			std::span<const char> GetDirectoryTable(const TagStruct& tag) const {
				for (const auto& table : DirectoryTableSpan())
					if (table.Tag == tag)
						return { m_bytes + *table.Offset - m_offsetInCollection, *table.Length };
				return {};
			}

			template<typename Table>
			Table::View TryGetTable() const {
				const auto s = GetDirectoryTable(Table::DirectoryTableTag);
				if (s.empty())
					return {};

				return Table::View(s);
			}
		};
	};

	struct TtcFile {
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

		class View {
			union {
				const TtcFile* m_obj;
				const char* m_bytes;
			};
			size_t m_length;
			size_t m_offsetInCollection;

		public:
			View() : m_obj(nullptr), m_length(0), m_offsetInCollection(0) {}
			View(std::nullptr_t) : View() {}
			View(decltype(m_obj) pObject, size_t length, size_t offsetInCollection = 0)
				: m_obj(pObject), m_length(length), m_offsetInCollection(offsetInCollection) {}
			View(View&&) = default;
			View(const View&) = default;
			View& operator=(View&&) = default;
			View& operator=(const View&) = default;
			View& operator=(std::nullptr_t) { m_obj = nullptr; m_length = 0; }
			View(const void* pData, size_t length) : View(std::span(reinterpret_cast<const char*>(pData), length)) {}
			template<typename T>
			View(std::span<T> data) : View() {
				if (data.size_bytes() < sizeof Header)
					return;

				const auto obj = reinterpret_cast<decltype(m_obj)>(&data[0]);

				if (obj->FileHeader.Tag != Header::HeaderTag)
					return;
				if (obj->FileHeader.MajorVersion == 0)
					return;
				if (data.size_bytes() < sizeof Header + sizeof uint32_t * obj->FileHeader.FontCount)
					return;
				if (obj->FileHeader.MajorVersion >= 2) {
					if (data.size_bytes() < sizeof Header + sizeof uint32_t * obj->FileHeader.FontCount + sizeof DigitalSignatureHeader)
						return;

					const auto pDsig = reinterpret_cast<const DigitalSignatureHeader*>(&obj->FontOffsets[*obj->FileHeader.FontCount]);
					if (pDsig->Tag.NativeValue == 0)
						void();
					else if (pDsig->Tag == DigitalSignatureHeader::HeaderTag) {
						if (data.size_bytes() < static_cast<size_t>(*pDsig->Offset) + *pDsig->Length)
							return;
					} else
						return;
				}
				for (size_t i = 0, i_ = *obj->FileHeader.FontCount; i < i_; i++) {
					const auto offset = static_cast<size_t>(*obj->FontOffsets[i]);
					if (SfntFile::View v(reinterpret_cast<const char*>(obj) + offset, data.size_bytes() - offset, offset); !v)
						return;
				}

				m_obj = obj;
				m_length = data.size_bytes();
			}

			operator bool() const {
				return !!m_obj;
			}

			decltype(m_obj) operator*() const {
				return m_obj;
			}

			decltype(m_obj) operator->() const {
				return m_obj;
			}

			size_t GetFontCount() const {
				return *m_obj->FileHeader.FontCount;
			}

			SfntFile::View GetFont(size_t index) const {
				if (index >= m_obj->FileHeader.FontCount)
					return {};

				const auto offset = static_cast<size_t>(*m_obj->FontOffsets[index]);
				return SfntFile::View(m_bytes + offset, m_length - offset, offset);
			}
		};
	};
}
#pragma pack(pop)

#endif
