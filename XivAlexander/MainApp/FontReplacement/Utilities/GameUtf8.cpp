#include "pch.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

#include <xivres/xivstring.h>

namespace GameUtf8 = XivAlexander::Apps::MainApp::FontReplacement::GameUtf8;

namespace {
	constexpr char32_t ReplacementChar = 0xFFFD;
}

uint32_t GameUtf8::Pack(char32_t codepoint) {
	const auto c = static_cast<uint32_t>(codepoint);
	if (c >= 0xD800 && c <= 0xDFFF)
		return Pack(ReplacementChar);
	if (c < 0x80)
		return c;
	if (c < 0x800)
		return 0xC080u | ((c << 2) & 0x1F00) | (c & 0x3F);
	if (c < 0x10000)
		return 0xE08080u | ((c << 4) & 0x0F0000) | ((c << 2) & 0x3F00) | (c & 0x3F);
	if (c < 0x110000)
		return 0xF0808080u | ((c << 6) & 0x07000000) | ((c << 4) & 0x3F0000) | ((c << 2) & 0x3F00) | (c & 0x3F);
	return Pack(ReplacementChar);
}

uint32_t GameUtf8::Pack(const uint8_t* p) {
	if (*p < 0x80)
		return *p;

	// CharacterLength is 1 for a stray continuation or invalid lead byte and 0 for a truncated sequence.
	const auto length = CharacterLength(p);
	if (length < 2)
		return Pack(ReplacementChar);

	uint32_t value = 0;
	for (auto i = 0; i < length; i++)
		value = (value << 8) | p[i];
	return value;
}

bool GameUtf8::TryDecode(const uint8_t* p, int length, char32_t& codepoint) {
	codepoint = ReplacementChar;
	if (length <= 0)
		return false;
	const auto b = p[0];
	int expected;
	uint32_t cp, min;
	if (b < 0x80) {
		if (length != 1)
			return false;
		codepoint = b;
		return true;
	}
	if (b < 0xC2)
		return false;
	if (b < 0xE0)
		expected = 2, cp = b & 0x1F, min = 0x80;
	else if (b < 0xF0)
		expected = 3, cp = b & 0x0F, min = 0x800;
	else if (b < 0xF5)
		expected = 4, cp = b & 0x07, min = 0x10000;
	else
		return false;
	if (length != expected)
		return false;
	for (auto i = 1; i < expected; i++) {
		if ((p[i] & 0xC0) != 0x80)
			return false;
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return false;
	codepoint = static_cast<char32_t>(cp);
	return true;
}

bool GameUtf8::TryUnpack(uint32_t value, char32_t& codepoint) {
	uint8_t bytes[4];
	const auto n = value > 0xFFFFFF ? 4 : value > 0xFFFF ? 3 : value > 0xFF ? 2 : 1;
	for (auto i = 0; i < n; i++)
		bytes[i] = static_cast<uint8_t>(value >> (8 * (n - 1 - i)));
	return TryDecode(bytes, n, codepoint);
}

int GameUtf8::CharacterLength(const uint8_t* p) {
	if (*p < 0xC0)
		return 1;
	if (*p < 0xE0)
		return p[1] ? 2 : 0;
	if (*p < 0xF0)
		return p[1] && p[2] ? 3 : 0;
	if (*p < 0xF8)
		return p[1] && p[2] && p[3] ? 4 : 0;
	return 1;
}

bool GameUtf8::TryReadInteger(const uint8_t* p, int& value, int& length) {
	size_t available = 0;
	while (available < 5 && p[available])
		available++;
	uint32_t v;
	const auto n = xivres::xivstring::read_integer({p, available}, v);
	if (!n || v > static_cast<uint32_t>(INT_MAX))
		return false;
	value = static_cast<int>(v);
	length = static_cast<int>(n);
	return true;
}

int GameUtf8::MacroLength(const uint8_t* p) {
	if (!p[1])
		return 0;
	int payload, n;
	if (!TryReadInteger(p + 2, payload, n))
		return 0;

	const auto total = 2LL + n + payload + 1;
	if (total > INT_MAX)
		return 0;
	for (auto i = 2LL + n; i < total - 1; i++) {
		if (!p[i])
			return 0;
	}
	return p[total - 1] == Etx ? static_cast<int>(total) : 0;
}
