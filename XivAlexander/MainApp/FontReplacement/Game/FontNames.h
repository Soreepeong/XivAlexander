#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;
	struct GameFontManager;

	// Lobby fonts load from one of two tables, picked by a module setting that isn't kept, so the table is found by matching font sizes.
	namespace FontNames {
		// In the order of the game's font table; empty if it wasn't read.
		[[nodiscard]] const std::vector<std::string>& Families();

		void Initialize();

		// With the sizes their names stand for.
		[[nodiscard]] std::vector<std::pair<std::string, float>> FontNamesOf(std::string_view family);

		[[nodiscard]] std::optional<std::string> TryGetFontName(GameFont* font);

		// While the lobby's fonts are loaded, this finds the lobby's font of the name.
		[[nodiscard]] GameFont* TryFindFont(std::string_view fontName);
	}
}
