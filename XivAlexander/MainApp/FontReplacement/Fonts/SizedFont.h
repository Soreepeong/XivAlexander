#pragma once

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Fonts/Records/GlyphPick.h"
#include "MainApp/FontReplacement/Utilities/AgedMap.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GlyphAtlas;
	class LoadedFont;

	// Shared by the copies of a family's fonts of one size, such as a font and its lobby version.
	// A bold one keeps the upright one's line metrics, so that lines partly in bold line up.
	class SizedFont {
		// A copy reads its sized font and atlas after a reset, until its next use.
		const std::shared_ptr<GlyphAtlas> m_atlas;

	public:
		LoadedFont* const Font;
		GlyphAtlas* const Atlas;
		GameFont* const Game;
		const float Px;
		const int Ascent;
		const int LineHeight;
		const bool Bold;

		// Largest first; only Font at the size of one of the family's fonts.
		const std::vector<std::pair<LoadedFont*, GameFont*>> Fonts;

		// A character's glyph, whatever made it, by packed UTF-8 value; nullptr if none could be made.
		// Holds each glyph once. The registry sweeps it, against one capacity for every sized font.
		AgedMap<uint32_t, GameGlyph*> Glyphs;

		std::unordered_map<char32_t, GlyphPick> Picks;

		SizedFont(
			LoadedFont* font, float px, int ascent, int lineHeight, GameFont* game, std::shared_ptr<GlyphAtlas> atlas,
			std::vector<std::pair<LoadedFont*, GameFont*>> fonts, bool bold, const FrameState& state);
		SizedFont(const SizedFont&) = delete;
		SizedFont& operator=(const SizedFont&) = delete;

		// Of several fonts, the largest whose element other than the game's has the grapheme, else the game's, else one without any for it.
		GlyphPick Pick(std::span<const char32_t> grapheme);

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
