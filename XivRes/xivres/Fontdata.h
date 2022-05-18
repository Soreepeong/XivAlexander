#ifndef _XIVRES_Fontdata_H_
#define _XIVRES_Fontdata_H_

#include <map>
#include <stdexcept>

#include "Internal/ByteOrder.h"
#include "Internal/Codepoint.h"

#include "Common.h"

namespace XivRes {
	struct FontdataHeader {
		static constexpr char Signature_Value[8] = {
			'f', 'c', 's', 'v', '0', '1', '0', '0',
		};

		char Signature[8]{};
		LE<uint32_t> FontTableHeaderOffset;
		LE<uint32_t> KerningHeaderOffset;
		uint8_t Padding_0x10[0x10]{};
	};
	static_assert(sizeof FontdataHeader == 0x20);

	struct FontdataGlyphTableHeader {
		static constexpr char Signature_Value[4] = {
			'f', 't', 'h', 'd',
		};

		char Signature[4]{};
		LE<uint32_t> FontTableEntryCount;
		LE<uint32_t> KerningEntryCount;
		uint8_t Padding_0x0C[4]{};
		LE<uint16_t> TextureWidth;
		LE<uint16_t> TextureHeight;
		LE<float> Points{ 0.f };
		LE<uint32_t> LineHeight;
		LE<uint32_t> Ascent;
	};
	static_assert(sizeof FontdataGlyphTableHeader == 0x20);

	struct FontdataGlyphEntry {
		LE<uint32_t> Utf8Value;
		LE<uint16_t> ShiftJisValue;
		LE<uint16_t> TextureIndex;
		LE<uint16_t> TextureOffsetX;
		LE<uint16_t> TextureOffsetY;
		LE<uint8_t> BoundingWidth;
		LE<uint8_t> BoundingHeight;
		LE<int8_t> NextOffsetX;
		LE<int8_t> CurrentOffsetY;

		[[nodiscard]] char32_t Char() const {
			return Internal::Utf8Uint32ToUnicodeCodePoint(Utf8Value);
		}

		char32_t Char(char32_t newValue) {
			Utf8Value = Internal::UnicodeCodePointToUtf8Uint32(newValue);
			ShiftJisValue = Internal::UnicodeCodePointToShiftJisUint16(newValue);
			return newValue;
		}

		uint16_t TextureFileIndex() const {
			return *TextureIndex >> 2;
		}

		uint16_t TexturePlaneIndex() const {
			return *TextureIndex & 3;
		}
	};
	static_assert(sizeof FontdataGlyphEntry == 0x10);

	struct FontdataKerningTableHeader {
		static constexpr char Signature_Value[4] = {
			'k', 'n', 'h', 'd',
		};

		char Signature[4]{};
		LE<uint32_t> EntryCount;
		uint8_t Padding_0x08[8]{};
	};
	static_assert(sizeof FontdataKerningTableHeader == 0x10);

	struct FontdataKerningEntry {
		LE<uint32_t> LeftUtf8Value;
		LE<uint32_t> RightUtf8Value;
		LE<uint16_t> LeftShiftJisValue;
		LE<uint16_t> RightShiftJisValue;
		LE<int32_t> RightOffset;

		[[nodiscard]] char32_t Left() const {
			return Internal::Utf8Uint32ToUnicodeCodePoint(LeftUtf8Value);
		}

		char32_t Left(char32_t newValue) {
			LeftUtf8Value = Internal::UnicodeCodePointToUtf8Uint32(newValue);
			LeftShiftJisValue = Internal::UnicodeCodePointToShiftJisUint16(newValue);
			return newValue;
		}

		[[nodiscard]] char32_t Right() const {
			return Internal::Utf8Uint32ToUnicodeCodePoint(RightUtf8Value);
		}

		char32_t Right(char32_t newValue) {
			RightUtf8Value = Internal::UnicodeCodePointToUtf8Uint32(newValue);
			RightShiftJisValue = Internal::UnicodeCodePointToShiftJisUint16(newValue);
			return newValue;
		}
	};
	static_assert(sizeof FontdataKerningEntry == 0x10);
}

#endif
