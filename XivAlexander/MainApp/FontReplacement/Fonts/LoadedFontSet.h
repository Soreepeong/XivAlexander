#pragma once

#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FaceSourceCache;
	class DirectWriteServices;
	class LoadedFont;
	class SystemFallbackFonts;

	// Fonts made together, sharing their face sources.
	class LoadedFontSet {
		std::shared_ptr<FaceSourceCache> m_sources;
		std::unique_ptr<LoadedFont> m_game;
		std::map<std::string, std::unique_ptr<LoadedFont>, LessIgnoringCase> m_preset;

		LoadedFontSet();

	public:
		LoadedFontSet(const LoadedFontSet&) = delete;
		LoadedFontSet& operator=(const LoadedFontSet&) = delete;
		~LoadedFontSet();

		// Only the game font's, to which the others are added.
		[[nodiscard]] static std::unique_ptr<LoadedFontSet> CreateGame(DirectWriteServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts);

		[[nodiscard]] const std::shared_ptr<FaceSourceCache>& Sources() const { return m_sources; }

		// The font of the game fonts the preset has no font of.
		[[nodiscard]] LoadedFont& Game() const { return *m_game; }

		[[nodiscard]] std::map<std::string, std::unique_ptr<LoadedFont>, LessIgnoringCase>& Preset() { return m_preset; }

		[[nodiscard]] LoadedFont& FontOf(const std::string& fontName) const;

		// A game font was rebuilt, possibly from another file.
		void ForgetGameFonts();
	};
}
