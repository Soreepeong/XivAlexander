#include "pch.h"
#include "MainApp/FontReplacement/Fonts/LoadedFontSet.h"

#include "MainApp/FontReplacement/Fonts/Faces/FaceSourceCache.h"
#include "MainApp/FontReplacement/Fonts/Faces/LoadedFont.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::LoadedFontSet::LoadedFontSet()
	: m_sources(std::make_shared<FaceSourceCache>()) {
}

FontReplacement::LoadedFontSet::~LoadedFontSet() = default;

std::unique_ptr<FontReplacement::LoadedFontSet> FontReplacement::LoadedFontSet::CreateGame(DirectWriteServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts) {
	auto set = std::unique_ptr<LoadedFontSet>(new LoadedFontSet());
	set->m_game = LoadedFont::CreateGame(services, systemFallback, fallbackFonts, set->m_sources);
	return set;
}

FontReplacement::LoadedFont& FontReplacement::LoadedFontSet::FontOf(const std::string& fontName) const {
	const auto it = m_preset.find(fontName);
	return it != m_preset.end() ? *it->second : *m_game;
}

void FontReplacement::LoadedFontSet::ForgetGameFonts() {
	for (const auto& font : m_preset | std::views::values)
		font->ForgetGameFonts();
	m_game->ForgetGameFonts();
}
