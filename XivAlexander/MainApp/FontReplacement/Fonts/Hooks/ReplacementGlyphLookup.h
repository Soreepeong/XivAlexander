#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/FontStructs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class CellPlacer;
	class CopyFonts;
	class CopyInfo;
	class FontSetFlags;
	class FrameState;
	class GameTextLayout;
	class GlyphFactory;
	class GlyphStore;
	class SizedFont;
	class SizedFontRegistry;

	// Which glyph GetGlyph returns for a copy.
	class GlyphLookup {
	public:
		// The UI scales (High Resolution UI) text wrapped unscaled may be drawn at, the largest last.
		static constexpr float WrapScales[]{1.f, 1.5f, 2.f, 3.f};

		// The game's own lookup in a font.
		using GameGlyphFunction = std::function<GameGlyph*(GameFontSet* set, uint32_t utf8Value, GameFont* font)>;

	private:
		// What LayOutCharacter lays out a character a font has no glyph of as, in order of preference, as packed UTF-8.
		static constexpr uint32_t MissingGlyphSubstitutes[]{0xE38093, 0x2D, 0xE39080};

		SizedFontRegistry& m_sized;
		GameTextLayout& m_layout;
		GlyphFactory& m_factory;
		CellPlacer& m_placer;
		GlyphStore& m_store;
		const FrameState& m_state;
		const FontSetFlags& m_flags;
		GameGlyphFunction m_gameGlyph;

	public:
		GlyphLookup(
			SizedFontRegistry& sized, GameTextLayout& layout, GlyphFactory& factory, CellPlacer& placer, GlyphStore& store, const FrameState& state,
			const FontSetFlags& flags, GameGlyphFunction gameGlyph);

		GameGlyph* Get(GameFontSet* set, uint32_t utf8Value, CopyInfo& info);

	private:
		// An italic macro sets the state's bit, which measuring sees too. A text node's italics are only in the font set's own flags, which only
		// drawing sees.
		[[nodiscard]] ItalicMode GetItalicMode(GameFontSet* set) const;

		GameGlyph* WithPixels(GameGlyph* glyph);

		// The sized fonts of the UI scales (WrapScales) for the text being wrapped, drawn (the largest) last.
		std::span<SizedFont* const> GetWrapSizes(GameFontSet* set, CopyInfo& info, SizedFont* drawn);

		// While wrapping, the character LayOutCharacter is at gets its cell of its run laid out at every UI scale
		// (GameTextLayout::TryGetWrapGlyph). nullptr if the character is looked up by itself.
		GameGlyph* GetWrapRunGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont* drawn);

		// While wrapping, for a character looked up by itself, in the copy of the largest UI scale, a glyph of no pixels as wide as glyph is at
		// the widest of the UI scales.
		GameGlyph* GetWrapGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont* drawn, GameGlyph* glyph);

		// nullptr if neither the copy nor its game font has a glyph of the character. With shape, the character LayOutCharacter is at gets its
		// shaped cluster's glyph.
		GameGlyph* GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& font, bool shape);

		// The sized font's own glyph, else a copy of its game fonts' or of the game's lookup's. Held once by the caller, or nullptr if there is
		// none.
		GameGlyph* MakeGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& font);

		SizedFont* GetDrawnSized(CopyInfo& info);

		// LayOutCharacter would take its substitutes from the font's glyph map directly, getting game glyphs of texture indices the copy has no
		// textures of, so the copy's own glyphs of them (else one of no width) are given instead.
		GameGlyph* GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& drawn);
	};
}
