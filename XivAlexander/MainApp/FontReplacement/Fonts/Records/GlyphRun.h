#pragma once

#include "MainApp/FontReplacement/Fonts/Records/GlyphOffset.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Glyphs of one source, with their advances as laid out and their offsets in pixels (empty: none). A view.
	struct GlyphRun {
		std::span<const uint32_t> GlyphIds;
		std::span<const float> Advances;
		std::span<const GlyphOffset> Offsets;
	};
}
