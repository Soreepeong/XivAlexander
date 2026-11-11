#pragma once

#include "MainApp/FontReplacement/Fonts/Hooks/UiScaleWrapMeasurer.h"
#include "MainApp/FontReplacement/FontStructs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GlyphAtlasPlacer;
	class ReplacementFontTable;
	class ReplacementFont;
	class FrameState;
	class ShapedRunCache;
	class GlyphFactory;
	class GlyphStore;
	class LayoutContext;
	class SizedFont;
	class SizedFontRegistry;

	// Which glyph GetGlyph returns for a copy.
	class ReplacementGlyphLookup {
	public:
		// The game's own lookup in a font.
		using GameGlyphFunction = std::function<GameGlyph*(GameFontSet* set, uint32_t utf8Value, GameFont* font)>;

	private:
		SizedFontRegistry& m_sized;
		ShapedRunCache& m_layout;
		GlyphFactory& m_factory;
		GlyphAtlasPlacer& m_placer;
		GlyphStore& m_store;
		const FrameState& m_state;
		LayoutContext& m_context;
		GameGlyphFunction m_gameGlyph;
		UiScaleWrapMeasurer m_wrap;

	public:
		ReplacementGlyphLookup(
			SizedFontRegistry& sized, ShapedRunCache& layout, GlyphFactory& factory, GlyphAtlasPlacer& placer, GlyphStore& store, const FrameState& state,
			LayoutContext& context, GameGlyphFunction gameGlyph);

		GameGlyph* Get(GameFontSet* set, uint32_t utf8Value, ReplacementFont& info);

		// nullptr if neither the copy nor its game font has a glyph of the character. With shape, the character LayOutCharacter is at gets its
		// shaped cluster's glyph.
		GameGlyph* GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont& font, bool shape);

	private:
		GameGlyph* WithPixels(GameGlyph* glyph);

		// The sized font's own glyph, else a copy of its game fonts' or of the game's lookup's. Held once by the caller, or nullptr if there is
		// none.
		GameGlyph* MakeGlyph(GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont& font);

		SizedFont* GetDrawnSized(ReplacementFont& info);

		// LayOutCharacter would take its substitutes from the font's glyph map directly, getting game glyphs of texture indices the copy has no
		// textures of, so the copy's own glyphs of them (else one of no width) are given instead.
		GameGlyph* GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont& drawn);
	};
}
