#pragma once

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Utilities/AgedMap.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GlyphAtlas;
	class MaterializedFont;
	class MaterializedFontElement;

	// Element is nullptr if none of Font's elements has the glyph.
	struct GlyphSource {
		MaterializedFont* Font = nullptr;
		GameFont* Game = nullptr;
		MaterializedFontElement* Element = nullptr;

		bool operator==(const GlyphSource&) const = default;
	};

	// Shared by the copies of a family's fonts of one size, such as a font and its lobby version.
	// A bold one keeps the upright one's line metrics, so that lines partly in bold line up.
	struct SizedFont {
		MaterializedFont* Font;
		GlyphAtlas* Atlas;
		GameFont* Game;
		float Px;
		int Ascent;
		int LineHeight;
		bool Bold;

		// Largest first; only Font at the size of one of the family's fonts.
		std::vector<std::pair<MaterializedFont*, GameFont*>> Fonts;

		// The glyphs below are held with FontReplacer::Keep, and let go of with FontReplacer::Release.

		// By packed UTF-8 value; nullptr for a glyph left to the game.
		AgedMap<uint32_t, GameGlyph*> Glyphs;

		// Copied from the game fonts of Fonts, by packed UTF-8 value; nullptr if none could be.
		AgedMap<uint32_t, GameGlyph*> GameGlyphs;

		AgedMap<std::pair<GameFont*, const GameGlyph*>, GameGlyph*> ConvertedGlyphs;

		// Glyphs of no pixels that advance as far as wrapping measures (FontReplacer::GetWrapGlyph), by advance.
		AgedMap<int, GameGlyph*> WrapCells;
		std::unordered_map<char32_t, GlyphSource> Picks;

		// Of several fonts, the largest whose element other than the game's has the grapheme, else the game's, else one without any for it.
		GlyphSource Pick(std::span<const char32_t> grapheme);
	};
}
