#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;

	// Names fonts as presets do (AXIS_12), from the game's font table and the lobby's two (the one whose sizes match is used); lobby fonts map to game faces.
	namespace GameFontNames {
		// In the order of the game's font table; empty if it wasn't read.
		[[nodiscard]] const std::vector<std::string>& Families();

		// Throws if the tables can't be found.
		void Initialize();

		// With the sizes their names stand for.
		[[nodiscard]] std::vector<std::pair<std::string, float>> FacesOf(std::string_view family);

		// nullopt if the font isn't one of the font manager's.
		[[nodiscard]] std::optional<std::string> GetFaceName(GameFont* font);

		// The game font's family (AXIS of AXIS_12, JupiterN of Jupiter_45), else the name before the last underscore (FontChanger.Presets' Preset.FamilyOf).
		[[nodiscard]] std::string FamilyOf(std::string_view faceName);
	}
}
