#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;

	// Names the game's fonts as presets do (AXIS_12, TrumpGothic_184).
	//
	// The fonts load from fixed tables of 41 entries { u64 texture count, char* texture name format, char* FDT name } (the
	// FontTables signature): one for the game, and two for the lobby, picked by a module setting that isn't kept. Lobby fonts
	// (their textures are font_lobby%d.tex) map to the faces of the game's (FontChanger's exportMapFontLobbyToFont); the
	// lobby table is the one whose sizes match the fonts.
	namespace GameFontNames {
		// Gets the game's font families (AXIS, JupiterN, ...) in the order of its font table; none if it wasn't read.
		[[nodiscard]] const std::vector<std::string>& Families();

		// Reads the tables. Throws if they can't be found.
		void Initialize();

		// Gets the faces of a family in the game's font table, with the sizes their names stand for.
		[[nodiscard]] std::vector<std::pair<std::string, float>> FacesOf(std::string_view family);

		// Gets the face name of a font of the font manager; nullopt if it isn't one.
		[[nodiscard]] std::optional<std::string> GetFaceName(GameFont* font);

		// Gets the family of a face: that of the game's font of the name (AXIS of AXIS_12, JupiterN of Jupiter_45), or the
		// name before the last underscore (FontChanger.Presets' Preset.FamilyOf).
		[[nodiscard]] std::string FamilyOf(std::string_view faceName);
	}
}
