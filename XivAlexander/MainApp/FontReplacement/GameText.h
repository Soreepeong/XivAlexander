#pragma once

#include "MainApp/FontReplacement/FontStructs.h"

// Steps through the game's text (SeString) as it does: characters by the first byte of their UTF-8 sequence, and macros
// (0x02, type, length, payload, 0x03) whole.
namespace XivAlexander::Apps::MainApp::FontReplacement::GameText {
	constexpr uint8_t MacroStart = 0x02;
	constexpr uint8_t MacroEnd = 0x03;

	// Decodes a SeString integer (a macro's payload length); returns the bytes it takes, 0 if malformed.
	inline int ReadInteger(const uint8_t* p, int& value) {
		const auto marker = p[0];
		if (marker == 0 || marker > 0xFE) {
			value = 0;
			return 0;
		}

		if (marker < 0xF0) {
			value = marker - 1;
			return 1;
		}

		// The marker's low bits say which bytes of the value (highest first) follow; the others are zero.
		const auto flags = marker + 1;
		auto n = 1;
		value = 0;
		for (auto bit = 3; bit >= 0; bit--) {
			if (flags & (1 << bit))
				value |= p[n++] << (8 * bit);
		}
		return n;
	}

	// Gets the length of the macro at p (which starts with MacroStart); 0 if it is malformed: a bad length, the end of the
	// text inside it, or no MacroEnd after its payload.
	inline int MacroLength(const uint8_t* p) {
		if (p[1] == 0)
			return 0;
		int payload;
		const auto n = ReadInteger(p + 2, payload);
		if (n == 0 || payload < 0)
			return 0;
		const auto total = 2 + n + payload + 1;
		for (auto i = 2; i < total - 1; i++) {
			if (p[i] == 0)
				return 0;
		}
		return p[total - 1] == MacroEnd ? total : 0;
	}

	// Gets the length of the character at p as the game steps (by its first byte alone, valid or not); 0 if it runs into
	// the end of the text.
	inline int CharacterLength(const uint8_t* p) {
		const auto n = GameUtf8::SequenceLength(*p);
		for (auto i = 1; i < n; i++) {
			if (p[i] == 0)
				return 0;
		}
		return n;
	}
}
