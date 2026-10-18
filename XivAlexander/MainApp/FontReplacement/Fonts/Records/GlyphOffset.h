#pragma once

#include <dwrite.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A glyph's offset from where its advance puts it; laid out as DWRITE_GLYPH_OFFSET.
	struct GlyphOffset {
		float Advance = 0;
		float Ascender = 0;

		bool operator==(const GlyphOffset&) const = default;
	};

	static_assert(sizeof(GlyphOffset) == sizeof(DWRITE_GLYPH_OFFSET));

	[[nodiscard]] inline const DWRITE_GLYPH_OFFSET* AsDWriteOffsets(std::span<const GlyphOffset> offsets) {
		return offsets.empty() ? nullptr : reinterpret_cast<const DWRITE_GLYPH_OFFSET*>(offsets.data());
	}
}
