#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// How a run the game draws in italics is shaped.
	enum class ItalicMode : uint8_t {
		Upright,

		// By an italic macro, which measuring sees too: shaped with the sources' italics, drawn as they are; the game shears
		// the glyphs of sources without one.
		Real,

		// By the text node, which only drawing sees (measured upright): shaped in italics, spaced out to the upright run's
		// width; where the two shapings differ, shaped upright, each single-character cluster drawn with its font's italic.
		Images,
	};
}
