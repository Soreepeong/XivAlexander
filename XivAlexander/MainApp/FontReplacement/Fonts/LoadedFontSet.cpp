#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontSet.h"

#include "MainApp/FontReplacement/Fonts/Faces/FaceSourceCache.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontSet::FontSet()
	: m_sources(std::make_shared<FaceSourceCache>()) {
}

FontReplacement::FontSet::~FontSet() = default;

std::unique_ptr<FontReplacement::FontSet> FontReplacement::FontSet::CreateGame(FontServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts) {
	auto set = std::unique_ptr<FontSet>(new FontSet());
	set->m_game = MaterializedFont::CreateGame(services, systemFallback, fallbackFonts, set->m_sources);
	return set;
}

FontReplacement::MaterializedFont& FontReplacement::FontSet::FontOf(const std::string& fontName) const {
	const auto it = m_preset.find(fontName);
	return it != m_preset.end() ? *it->second : *m_game;
}

void FontReplacement::FontSet::ForgetGameFonts() {
	for (const auto& font : m_preset | std::views::values)
		font->ForgetGameFonts();
	m_game->ForgetGameFonts();
}
