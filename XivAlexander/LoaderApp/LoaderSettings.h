#pragma once

#include "Config/Enums.h"

namespace XivAlexander::LoaderApp {
	/// The loader's own preferences in loader.json of the config folder; config.runtime.json is kept per game.
	struct LoaderSettings {
		std::optional<Language> UiLanguage;
		std::optional<ThemeMode> UiThemeMode;
		bool Advanced = false;
		std::vector<std::filesystem::path> AddedPaths;

		static LoaderSettings Load(const std::filesystem::path& configDir);

		/// Keeps keys it does not know.
		void Save(const std::filesystem::path& configDir) const;

		/// The saved choice, else the first one found in config.runtime.json, else the system's.
		[[nodiscard]] Language EffectiveLanguage(const std::filesystem::path& configDir) const;
		[[nodiscard]] ThemeMode EffectiveThemeMode(const std::filesystem::path& configDir) const;
	};

	[[nodiscard]] WORD LangIdOf(Language language);
}
