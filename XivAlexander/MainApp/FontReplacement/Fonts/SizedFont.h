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
	class SizedFont {
	public:
		MaterializedFont* const Font;
		GlyphAtlas* const Atlas;
		GameFont* const Game;
		const float Px;
		const int Ascent;
		const int LineHeight;
		const bool Bold;

		// Largest first; only Font at the size of one of the family's fonts.
		const std::vector<std::pair<MaterializedFont*, GameFont*>> Fonts;

		// A character's glyph, whatever made it, by packed UTF-8 value; nullptr if none could be made.
		// Holds each glyph once. The registry sweeps it, against one capacity for every sized font.
		AgedMap<uint32_t, GameGlyph*> Glyphs;

		std::unordered_map<char32_t, GlyphSource> Picks;

		SizedFont(
			MaterializedFont* font, float px, int ascent, int lineHeight, GameFont* game, GlyphAtlas* atlas,
			std::vector<std::pair<MaterializedFont*, GameFont*>> fonts, bool bold, const FrameState& state);
		SizedFont(const SizedFont&) = delete;
		SizedFont& operator=(const SizedFont&) = delete;

		// Of several fonts, the largest whose element other than the game's has the grapheme, else the game's, else one without any for it.
		GlyphSource Pick(std::span<const char32_t> grapheme);

		void CollectUsed(std::vector<uint64_t>& used) const { Glyphs.CollectUsed(used); }

		template<typename TRelease>
		void ReleaseGlyphsUsedBefore(uint64_t frame, TRelease&& release) {
			Glyphs.DropUsedBefore(frame, [](GameGlyph*) { return true; }, release);
		}

		template<typename TRelease>
		void ReleaseGlyphs(TRelease&& release) {
			Glyphs.Clear(release);
		}
	};
}
