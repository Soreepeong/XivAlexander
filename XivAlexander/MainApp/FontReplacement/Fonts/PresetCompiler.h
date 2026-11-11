#pragma once

#include "MainApp/FontReplacement/Fonts/Records/FontRequest.h"
#include "MainApp/FontReplacement/Fonts/Records/FontSettings.h"
#include "MainApp/FontReplacement/Fonts/Records/LoadReport.h"

// Reads a request's presets and makes its system fonts' presets, on the loader's thread.
namespace XivAlexander::Apps::MainApp::FontReplacement::PresetCompiler {
	// Makes the settings of the fonts request asks for. Each game font family takes fonts only from its own sources.
	// keepOnFailure is whether a source that can't be loaded leaves the fonts loaded last in place.
	// Returns the fonts' settings, nullopt if the fonts loaded last are kept, and what to show and watch.
	[[nodiscard]] std::pair<std::optional<FontSettings>, LoadReport> Compile(const FontRequest& request, bool keepOnFailure);
}
