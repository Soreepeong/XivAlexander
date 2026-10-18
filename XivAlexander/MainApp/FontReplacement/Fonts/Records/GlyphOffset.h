#pragma once

#include <dwrite.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Laid out as DWRITE_GLYPH_OFFSET.
	struct GlyphOffset {
		float Advance = 0;
		float Ascender = 0;

		bool operator==(const GlyphOffset&) const = default;
	};

	static_assert(sizeof(GlyphOffset) == sizeof(DWRITE_GLYPH_OFFSET));
}
