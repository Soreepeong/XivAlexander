#pragma once

#include "MainApp/FontReplacement/Fonts/Records/GlyphOffset.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Offsets are in pixels, and empty if there are none.
	struct GlyphRun {
		std::span<const uint32_t> GlyphIds;
		std::span<const float> Advances;
		std::span<const GlyphOffset> Offsets;
	};
}
