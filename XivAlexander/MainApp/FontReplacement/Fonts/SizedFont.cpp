#include "pch.h"
#include "MainApp/FontReplacement/Fonts/SizedFont.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::SizedFont::SizedFont(
	MaterializedFont* font, float px, int ascent, int lineHeight, GameFont* game, GlyphAtlas* atlas,
	std::vector<std::pair<MaterializedFont*, GameFont*>> fonts, bool bold, const FrameState& state)
	: Font(font)
	, Atlas(atlas)
	, Game(game)
	, Px(px)
	, Ascent(ascent)
	, LineHeight(lineHeight)
	, Bold(bold)
	, Fonts(std::move(fonts))
	, Glyphs(state, (std::numeric_limits<size_t>::max)()) {
}

FontReplacement::GlyphSource FontReplacement::SizedFont::Pick(std::span<const char32_t> grapheme) {
	if (grapheme.size() == 1) {
		if (const auto it = Picks.find(grapheme[0]); it != Picks.end())
			return it->second;
	}

	GlyphSource pick{Font, Game, Font->TryGetElement(grapheme, Game)};
	if (Fonts.size() > 1) {
		std::optional<GlyphSource> none, game, own;
		for (const auto& [font, gameFont] : Fonts) {
			const auto element = font->TryGetElement(grapheme, gameFont);
			if (element && !element->DrawsGame()) {
				own = GlyphSource{font, gameFont, element};
				break;
			}

			if (!element) {
				if (!none)
					none = GlyphSource{font, gameFont, nullptr};
			} else if (!game) {
				game = GlyphSource{font, gameFont, element};
			}
		}

		pick = own ? *own : game ? *game : none ? *none : pick;
	}

	if (grapheme.size() == 1)
		Picks.emplace(grapheme[0], pick);
	return pick;
}
