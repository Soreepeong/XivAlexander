#include "pch.h"
#include "Sqex_FontCsv.h"

#include "Sqex_Sqpack.h"

const char Sqex::FontCsv::FontCsvHeader::Signature_Value[8] = {
	'f', 'c', 's', 'v', '0', '1', '0', '0',
};

const char Sqex::FontCsv::FontTableHeader::Signature_Value[4] = {
	'f', 't', 'h', 'd',
};

const char Sqex::FontCsv::KerningHeader::Signature_Value[4] = {
	'k', 'n', 'h', 'd',
};

char32_t Sqex::FontCsv::Utf8Uint32ToUnicodeCodePoint(uint32_t n) {
	if ((n & 0xFFFFFF80) == 0)
		return static_cast<char32_t>(n & 0x7F);
	else if ((n & 0xFFFFE0C0) == 0xC080)
		return static_cast<char32_t>(
			((n >> 0x08) & 0x1F) |
			((n >> 0x00) & 0x3F)
		);
	else if ((n & 0xF0C0C0) == 0xE08080)
		return static_cast<char32_t>(
			((n >> 0x10) & 0x0F) |
			((n >> 0x08) & 0x3F) |
			((n >> 0x00) & 0x3F)
		);
	else if ((n & 0xF8C0C0C0) == 0xF0808080)
		return static_cast<char32_t>(
			((n >> 0x18) & 0x07) |
			((n >> 0x10) & 0x3F) |
			((n >> 0x08) & 0x3F) |
			((n >> 0x00) & 0x3F)
		);
	else
		return 0xFFFF;  // Guaranteed non-unicode
}

uint32_t Sqex::FontCsv::UnicodeCodePointToUtf8Uint32(char32_t codepoint) {
	if (codepoint <= 0x7F) {
		return codepoint;
	} else if (codepoint <= 0x7FF) {
		return ((0xC0 | ((codepoint >> 6))) << 8)
			| ((0x80 | ((codepoint >> 0) & 0x3F)) << 0);
	} else if (codepoint <= 0xFFFF) {
		return ((0xE0 | ((codepoint >> 12))) << 16)
			| ((0x80 | ((codepoint >> 6) & 0x3F)) << 8)
			| ((0x80 | ((codepoint >> 0) & 0x3F)) << 0);
	} else if (codepoint <= 0x10FFFF) {
		return ((0xF0 | ((codepoint >> 18))) << 24)
			| ((0x80 | ((codepoint >> 12) & 0x3F)) << 16)
			| ((0x80 | ((codepoint >> 6) & 0x3F)) << 8)
			| ((0x80 | ((codepoint >> 0) & 0x3F)) << 0);
	} else
		throw std::invalid_argument("Unicode code point is currently capped at 0x10FFFF.");
}

uint16_t Sqex::FontCsv::UnicodeCodePointToShiftJisUint16(char32_t codepoint) {
	wchar_t utf16[2]{};
	int utf16len = 0;
	if (codepoint <= 0xFFFF) {
		utf16[utf16len++] = static_cast<wchar_t>(codepoint);
	} else if (codepoint <= 0x10FFFF) {
		utf16[utf16len++] = static_cast<wchar_t>(0xD800 | ((codepoint >> 10) & 0x3FF));
		utf16[utf16len++] = static_cast<wchar_t>(0xDC00 | ((codepoint >> 0) & 0x3FF));
	}

	// https://docs.microsoft.com/en-us/windows/win32/intl/code-page-identifiers
	constexpr UINT CodePage_ShiftJis = 932;

	switch (char buf[2]; WideCharToMultiByte(CodePage_ShiftJis, 0,
	                                         utf16, utf16len,
	                                         buf, sizeof buf, nullptr, nullptr)) {
	case 1:
		return buf[0];
	case 2:
		return static_cast<uint16_t>((buf[0] << 8) | buf[1]);
	default:
		return ' ';
	}
}

std::u32string Sqex::FontCsv::ToU32(const std::string& s) {
	return std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>().from_bytes(s);
}

std::wstring Sqex::FontCsv::ToU16(const std::string& s) {
	return std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().from_bytes(s);
}

std::string Sqex::FontCsv::ToU8(const std::u32string& s) {
	return std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>().to_bytes(s);
}

char32_t Sqex::FontCsv::FontTableEntry::Char() const {
	return Utf8Uint32ToUnicodeCodePoint(Utf8Value);
}

char32_t Sqex::FontCsv::FontTableEntry::Char(char32_t newValue) {
	Utf8Value = UnicodeCodePointToUtf8Uint32(newValue);
	ShiftJisValue = UnicodeCodePointToShiftJisUint16(newValue);
	return newValue;
}

char32_t Sqex::FontCsv::KerningEntry::Left() const {
	return Utf8Uint32ToUnicodeCodePoint(LeftUtf8Value);
}

char32_t Sqex::FontCsv::KerningEntry::Left(char32_t newValue) {
	LeftUtf8Value = UnicodeCodePointToUtf8Uint32(newValue);
	LeftShiftJisValue = UnicodeCodePointToShiftJisUint16(newValue);
	return newValue;
}

char32_t Sqex::FontCsv::KerningEntry::Right() const {
	return Utf8Uint32ToUnicodeCodePoint(RightUtf8Value);
}

char32_t Sqex::FontCsv::KerningEntry::Right(char32_t newValue) {
	RightUtf8Value = UnicodeCodePointToUtf8Uint32(newValue);
	RightShiftJisValue = UnicodeCodePointToShiftJisUint16(newValue);
	return newValue;
}
