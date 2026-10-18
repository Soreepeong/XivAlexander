#pragma once

#include "MainApp/FontReplacement/Fonts/Records/GlyphOffset.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Glyphs of the source that shaped it, and the code units it covers (relative to the text shaped). Offsets empty: none.
	struct ShapedCluster {
		std::vector<uint32_t> GlyphIds;
		std::vector<float> Advances;
		std::vector<GlyphOffset> Offsets;
		int TextStart = 0;
		int TextEnd = 0;
	};
}
