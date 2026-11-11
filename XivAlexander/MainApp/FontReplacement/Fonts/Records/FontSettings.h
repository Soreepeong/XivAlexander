#pragma once

#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Game fonts the preset has no font of keep the game's glyphs, and characters a font lacks come from the game's glyphs unless
	// SystemFallback.
	struct FontSettings {
		Presets::Fonts Preset;
		bool SystemFallback = true;
	};
}
