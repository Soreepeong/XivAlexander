#pragma once
#include "Sqex_Common.h"

namespace Sqex::FontCsv {
	struct FontCsvHeader {
		static const char Signature_Value[8];
		char Signature[8]{};
		LE<uint32_t> FontTableHeaderOffset;
		LE<uint32_t> KerningHeaderOffset;
		uint8_t Padding_0x10[0x10]{};
	};
	static_assert(sizeof FontCsvHeader == 0x20);

	struct FontTableHeader {
		static const char Signature_Value[4];
		char Signature[4]{};
		LE<uint32_t> FontTableEntryCount;
		LE<uint32_t> KerningEntryCount;
		uint8_t Padding_0x0C[4]{};
		LE<uint16_t> TextureWidth;
		LE<uint16_t> TextureHeight;
		LE<float> Points{0.f};
		LE<uint32_t> LineHeight;
		LE<uint32_t> Ascent;
	};
	static_assert(sizeof FontTableHeader == 0x20);

	struct FontTableEntry {
		LE<uint32_t> Utf8Value;
		LE<uint16_t> ShiftJisValue;
		LE<uint16_t> TextureIndex;
		LE<uint16_t> TextureOffsetX;
		LE<uint16_t> TextureOffsetY;
		LE<uint8_t> BoundingWidth;
		LE<uint8_t> BoundingHeight;
		LE<uint8_t> NextOffsetX;
		LE<uint8_t> CurrentOffsetY;
		
		[[nodiscard]] char32_t Char() const;
		char32_t Char(char32_t newValue);
	};
	static_assert(sizeof FontTableEntry == 0x10);

	struct KerningHeader {
		static const char Signature_Value[4];
		char Signature[4]{};
		LE<uint32_t> EntryCount;
		uint8_t Padding_0x08[8]{};
	};
	static_assert(sizeof KerningHeader == 0x10);

	struct KerningEntry {
		LE<uint32_t> LeftUtf8Value;
		LE<uint32_t> RightUtf8Value;
		LE<uint16_t> LeftShiftJisValue;
		LE<uint16_t> RightShiftJisValue;
		LE<int32_t> RightOffset;

		[[nodiscard]] char32_t Left() const;
		char32_t Left(char32_t newValue);
		[[nodiscard]] char32_t Right() const;
		char32_t Right(char32_t newValue);
	};
	static_assert(sizeof KerningEntry == 0x10);

	class FontCsvData : public RandomAccessStream {
		FontCsvHeader m_fcsv;
		FontTableHeader m_fthd;
		std::vector<FontTableEntry> m_fontTableEntries;
		KerningHeader m_knhd;
		std::vector<KerningEntry> m_kerningEntries;

	public:
		FontCsvData(float pt, uint16_t textureWidth, uint16_t textureHeight);
		FontCsvData(const RandomAccessStream& stream, bool strict = false);
		
		[[nodiscard]] uint32_t StreamSize() const override;
		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;

		[[nodiscard]] const FontTableEntry* GetFontEntry(char32_t c) const;
		[[nodiscard]] int GetKerningDistance(char32_t l, char32_t r) const;
		[[nodiscard]] const std::vector<FontTableEntry>& GetFontTableEntries() const;
		[[nodiscard]] const std::vector<KerningEntry>& GetKerningEntries() const;

		void AddFontEntry(char32_t c, uint16_t textureIndex, uint16_t textureOffsetX, uint16_t textureOffsetY, uint8_t boundingWidth, uint8_t boundingHeight, uint8_t nextOffsetX, uint8_t currentOffsetY);
		void AddKerning(char32_t l, char32_t r, int rightOffset);
	};
}
