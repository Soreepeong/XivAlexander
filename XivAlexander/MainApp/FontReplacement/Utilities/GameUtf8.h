#pragma once

// Conversions between codepoints and the packed UTF-8 values the game keys its glyphs by (GameGlyph::Utf8Value).
namespace XivAlexander::Apps::MainApp::FontReplacement::GameUtf8 {
	constexpr uint8_t Stx = 0x02;
	constexpr uint8_t Etx = 0x03;

	// Invalid codepoints pack as U+FFFD.
	[[nodiscard]] uint32_t Pack(char32_t codepoint);

	// The character at p, stepped as the game does; U+FFFD for a broken sequence.
	[[nodiscard]] uint32_t Pack(const uint8_t* p);

	// false if the length bytes at p aren't exactly one valid UTF-8 sequence.
	[[nodiscard]] bool TryDecode(const uint8_t* p, int length, char32_t& codepoint);

	[[nodiscard]] bool TryUnpack(uint32_t value, char32_t& codepoint);

	// By the lead byte, as the game steps through text; 0 if the text ends inside it.
	[[nodiscard]] int CharacterLength(const uint8_t* p);

	// A macro's integer at p, within a null-terminated text.
	[[nodiscard]] bool TryReadInteger(const uint8_t* p, int& value, int& length);

	// Of the macro at p (at its STX), through its ETX; 0 if it is broken or runs into the text's end.
	[[nodiscard]] int MacroLength(const uint8_t* p);
}
