#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FaceSourceCache;
	class FontServices;
	class MaterializedFont;
	class SystemFallbackFonts;

	// Fonts made together, sharing their face sources.
	class FontSet {
		std::shared_ptr<FaceSourceCache> m_sources;
		std::unique_ptr<MaterializedFont> m_game;
		std::map<std::string, std::unique_ptr<MaterializedFont>, LessIgnoringCase> m_preset;

		FontSet();

	public:
		FontSet(const FontSet&) = delete;
		FontSet& operator=(const FontSet&) = delete;
		~FontSet();

		// Only the game font's, to which the others are added.
		[[nodiscard]] static std::unique_ptr<FontSet> CreateGame(FontServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts);

		[[nodiscard]] const std::shared_ptr<FaceSourceCache>& Sources() const { return m_sources; }

		// The font of the game fonts the preset has no font of.
		[[nodiscard]] MaterializedFont& Game() const { return *m_game; }

		[[nodiscard]] std::map<std::string, std::unique_ptr<MaterializedFont>, LessIgnoringCase>& Preset() { return m_preset; }

		[[nodiscard]] MaterializedFont& FontOf(const std::string& fontName) const;

		// A game font was rebuilt, possibly from another file.
		void ForgetGameFonts();
	};
}
