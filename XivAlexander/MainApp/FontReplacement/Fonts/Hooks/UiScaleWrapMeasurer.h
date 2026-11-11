#pragma once

#include "MainApp/FontReplacement/FontStructs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class ReplacementFont;
	class ShapedRunCache;
	class ReplacementGlyphLookup;
	class GlyphStore;
	class LayoutContext;
	class SizedFont;
	class SizedFontRegistry;

	// Text the wrapper lays out has no scale to be drawn at, and glyphs of different sizes don't scale evenly, so each character is measured
	// at every UI scale and advances as far as at its widest (LayoutContext::IsWrapMeasure).
	class UiScaleWrapMeasurer {
		// The UI scales (High Resolution UI) text wrapped unscaled may be drawn at, the largest last.
		static constexpr float Scales[]{1.f, 1.5f, 2.f, 3.f};

		SizedFontRegistry& m_sized;
		ShapedRunCache& m_layout;
		GlyphStore& m_store;
		LayoutContext& m_context;

	public:
		// The scale copies are picked for while wrapping, in whose pixels the text is measured.
		static constexpr float LargestScale = Scales[std::size(Scales) - 1];

		UiScaleWrapMeasurer(SizedFontRegistry& sized, ShapedRunCache& layout, GlyphStore& store, LayoutContext& context)
			: m_sized(sized)
			, m_layout(layout)
			, m_store(store)
			, m_context(context) {}

		// While wrapping, the character LayOutCharacter is at gets its cell of its run laid out at every UI scale
		// (ShapedRunCache::TryGetWrapGlyph). nullptr if the character is looked up by itself.
		GameGlyph* GetRunGlyph(GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont* drawn);

		// While wrapping, for a character looked up by itself, in the copy of the largest UI scale, a glyph of no pixels as wide as glyph is at
		// the widest of the UI scales. lookup looks the character up at the other scales.
		GameGlyph* Widen(ReplacementGlyphLookup& lookup, GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont* drawn, GameGlyph* glyph);

	private:
		// The sized fonts of the UI scales for the text being wrapped, drawn (the largest) last.
		std::span<SizedFont* const> GetSizes(GameFontSet* set, ReplacementFont& info, SizedFont* drawn);
	};
}
