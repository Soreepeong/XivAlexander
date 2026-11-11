#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"

#include "MainApp/FontReplacement/Fonts/ReplacementFontTable.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::FrameState::Fail(std::string_view message, const std::exception& e) {
	m_isOn = false;
	ReplacementFontTable::ForgetPicked();
	Fail(PluginFeature::Replacer, message, e);
}

void FontReplacement::FrameState::Fail(PluginFeature part, std::string_view message, const std::exception& e) {
	Host::Error("{}; turning it off: {}", message, e.what());
	m_failed.insert(part);
}

FontReplacement::FrameState::Snapshot FontReplacement::FrameState::Drain() {
	return {std::exchange(m_gameFontsChanged, false), std::exchange(m_planeReused, false)};
}
