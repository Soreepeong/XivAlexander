#pragma once

#include "MainApp/FontReplacement/Fonts/Records/GlyphOffset.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// TextStart and TextEnd are code units relative to the text shaped, and Offsets is empty if there are none.
	struct ShapedCluster {
		std::vector<uint32_t> GlyphIds;
		std::vector<float> Advances;
		std::vector<GlyphOffset> Offsets;
		int TextStart = 0;
		int TextEnd = 0;
	};
}
