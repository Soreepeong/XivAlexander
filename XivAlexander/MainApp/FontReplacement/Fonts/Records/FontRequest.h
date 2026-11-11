#pragma once

#include "Config/FontReplacementConfigs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The settings the fonts are made from.
	struct FontRequest {
		// The sources, by family.
		std::map<std::string, FontReplacementFamily> Families;

		// Whether characters a font lacks come from the system's fonts.
		bool SystemFallback = true;

		// What the sources' presets are relative to.
		std::filesystem::path PresetFolder;

		bool operator==(const FontRequest&) const = default;
	};
}
