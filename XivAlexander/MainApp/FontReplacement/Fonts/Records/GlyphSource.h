#pragma once

#include "MainApp/FontReplacement/FontStructs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class MaterializedFont;
	class MaterializedFontElement;

	// Element is nullptr if none of Font's elements has the glyph.
	struct GlyphSource {
		MaterializedFont* Font = nullptr;
		GameFont* Game = nullptr;
		MaterializedFontElement* Element = nullptr;

		bool operator==(const GlyphSource&) const = default;
	};
}
