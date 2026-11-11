#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontLifecycleHooks.h"

#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontLifecycleHooks::FontLifecycleHooks(std::function<void(ResetScope)> reset, FrameState& state)
	: m_reset(std::move(reset))
	, m_state(state) {
	const auto font = ResolveGameCode(Game::Resolved::Fonts::Font);
	m_buildFontHook.emplace("BuildFont", reinterpret_cast<uintptr_t>(font.BuildFont), [this](uintptr_t manager, uint16_t index) { return BuildFontDetour(manager, index); });
	m_unloadFontsHook.emplace("UnloadFonts", reinterpret_cast<uintptr_t>(font.UnloadFont), [this](uintptr_t manager) { return UnloadFontsDetour(manager); });
}

int FontReplacement::FontLifecycleHooks::BuildFontDetour(uintptr_t manager, uint16_t index) {
	const auto result = m_buildFontHook->Original(manager, index);
	FontsChanged("Refreshing the copies of a rebuilt font failed");
	return result;
}

uintptr_t FontReplacement::FontLifecycleHooks::UnloadFontsDetour(uintptr_t manager) {
	const auto result = m_unloadFontsHook->Original(manager);
	FontsChanged("Letting go of the unloaded fonts failed");
	return result;
}

void FontReplacement::FontLifecycleHooks::FontsChanged(const char* failure) {
	try {
		m_reset(ResetScope::SizedFonts);
		m_state.GameFontChanged();
	} catch (const std::exception& e) {
		m_state.Fail(failure, e);
	}
}
