#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/SubpixelSource.h"

#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::SubpixelShaders::SettingsRecord FontReplacement::SubpixelSource::Get(FontReplacementSubpixelMode mode, const FontReplacementClearTypeConfig& clearType, HWND window) {
	const auto setting = std::make_pair(mode, clearType);
	const auto now = GetTickCount64();
	if (setting == m_setting && (mode == FontReplacementSubpixelMode::Off || now - m_lastQuery < RefreshMs))
		return m_wanted;

	m_setting = setting;
	m_lastQuery = now;
	try {
		m_wanted = SubpixelShaders::SettingsRecord::From(mode, clearType, window);
	} catch (const std::exception& e) {
		Host::Error("Reading the subpixel text setting failed: {}", e.what());
	}
	return m_wanted;
}
