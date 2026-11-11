#pragma once

#include "MainApp/FontReplacement/FontStructs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class LoadedFont;
	class LoadedFontElement;

	// Element is nullptr if none of Font's elements has the glyph.
	struct GlyphPick {
		LoadedFont* Font = nullptr;
		GameFont* Game = nullptr;
		LoadedFontElement* Element = nullptr;

		bool operator==(const GlyphPick&) const = default;
	};
}
