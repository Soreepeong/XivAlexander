#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;
	struct GameFontManager;

	// Names fonts as presets do (AXIS_12), from the game's font table and the lobby's two (the one whose sizes match is used); lobby fonts map to game fonts.
	namespace FontNames {
		// In the order of the game's font table; empty if it wasn't read.
		[[nodiscard]] const std::vector<std::string>& Families();

		// Throws if the tables can't be found.
		void Initialize();

		// With the sizes their names stand for.
		[[nodiscard]] std::vector<std::pair<std::string, float>> FontNamesOf(std::string_view family);

		// nullopt if the font isn't one of the font manager's.
		[[nodiscard]] std::optional<std::string> TryGetFontName(GameFont* font);

		// The font manager's font of the name (the lobby's while the lobby's fonts are loaded); nullptr if none.
		[[nodiscard]] GameFont* TryFindFont(std::string_view fontName);
	}
}
