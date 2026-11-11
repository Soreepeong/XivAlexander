#include "pch.h"
#include "MainApp/FontReplacement/Fonts/SizedFont.h"

#include "MainApp/FontReplacement/Fonts/Faces/LoadedFont.h"
#include "MainApp/FontReplacement/Fonts/Faces/LoadedFontElement.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::SizedFont::SizedFont(
	LoadedFont* font, float px, int ascent, int lineHeight, GameFont* game, std::shared_ptr<GlyphAtlas> atlas,
	std::vector<std::pair<LoadedFont*, GameFont*>> fonts, bool bold, const FrameState& state)
	: m_atlas(std::move(atlas))
	, Font(font)
	, Atlas(m_atlas.get())
	, Game(game)
	, Px(px)
	, Ascent(ascent)
	, LineHeight(lineHeight)
	, Bold(bold)
	, Fonts(std::move(fonts))
	, Glyphs(state, (std::numeric_limits<size_t>::max)()) {
}

FontReplacement::GlyphPick FontReplacement::SizedFont::Pick(std::span<const char32_t> grapheme) {
	if (grapheme.size() == 1) {
		if (const auto it = Picks.find(grapheme[0]); it != Picks.end())
			return it->second;
	}

	GlyphPick pick{Font, Game, Font->TryGetElement(grapheme, Game)};
	if (Fonts.size() > 1) {
		std::optional<GlyphPick> none, game, own;
		for (const auto& [font, gameFont] : Fonts) {
			const auto element = font->TryGetElement(grapheme, gameFont);
			if (element && !element->DrawsGame()) {
				own = GlyphPick{font, gameFont, element};
				break;
			}

			if (!element) {
				if (!none)
					none = GlyphPick{font, gameFont, nullptr};
			} else if (!game) {
				game = GlyphPick{font, gameFont, element};
			}
		}

		pick = own ? *own : game ? *game : none ? *none : pick;
	}

	if (grapheme.size() == 1)
		Picks.emplace(grapheme[0], pick);
	return pick;
}
