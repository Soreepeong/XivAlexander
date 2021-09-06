#pragma once

#include "Sqex.h"

namespace Sqex::FontCsv {
	char32_t Utf8Uint32ToUnicodeCodePoint(uint32_t n);
	uint32_t UnicodeCodePointToUtf8Uint32(char32_t codepoint);
	uint16_t UnicodeCodePointToShiftJisUint16(char32_t codepoint);
	std::u32string ToU32(const std::string& s);

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
		LE<int8_t> NextOffsetX;
		LE<int8_t> CurrentOffsetY;
		
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
}
