#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The subpixel settings wanted, read from the system every RefreshMs while on, and at once when the setting changes.
	class SubpixelSettingsSource {
		// The window may move to another monitor.
		static constexpr uint64_t RefreshMs = 2000;

		std::optional<std::pair<FontReplacementSubpixelMode, FontReplacementClearTypeConfig>> m_setting;
		SubpixelShaders::SettingsRecord m_wanted;
		uint64_t m_lastQuery = 0;

	public:
		SubpixelShaders::SettingsRecord Get(FontReplacementSubpixelMode mode, const FontReplacementClearTypeConfig& clearType, HWND window);
	};
}
