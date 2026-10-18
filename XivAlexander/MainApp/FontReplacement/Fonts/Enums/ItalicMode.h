#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	enum class ItalicMode : uint8_t {
		Upright,

		// By an italic macro, which measuring sees too: the sources' italics drawn as they are, the others sheared by the game.
		Real,

		// By the text node, which measuring doesn't see: shaped as Real and spaced to the upright run's width, or where the shapings differ,
		// upright with each single-character cluster in its font's italic.
		Images,
	};
}
