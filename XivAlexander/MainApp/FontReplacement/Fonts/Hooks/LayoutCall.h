#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The character LayOutCharacter is laying out, for GetGlyph, which isn't told which character of the text it is.
	// Static rather than per thread, as the game lays text out and draws only on its main thread.
	struct LayoutCall {
		static LayoutCall Current;

		// While the text wrapper lays text out.
		static bool Wrapping;

		const uint8_t* Character = nullptr;

		// Laid out by an analyzer that only measures, so its glyph needs no pixels yet.
		bool MeasuringOnly = false;

		uintptr_t State = 0;

		// The italic bit was cleared for a real italic glyph, to be set again once the character is laid out.
		bool ItalicCleared = false;
	};

	inline LayoutCall LayoutCall::Current{};
	inline bool LayoutCall::Wrapping = false;
}
