#pragma once

#include "MainApp/FontReplacement/Fonts/FontLoader.h"
#include "Utils/Win32/Closeable.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Asks the loader for fonts when the settings they are made from or a watched file change. Game thread.
	class PresetController {
		using FolderWatch = Utils::Win32::Closeable<HANDLE, FindCloseChangeNotification>;

		const std::shared_ptr<Config> m_config;
		FontLoader& m_loader;
		const std::filesystem::path m_presetFolder;

		// By folder. A folder that can't be watched keeps an empty handle, so that it isn't tried at every change of the list.
		std::map<std::filesystem::path, FolderWatch> m_presetWatchers;
		std::map<std::filesystem::path, FolderWatch> m_folderWatchers;

		// By when each was last seen written (nullopt if missing then), as other files in a preset's folder change too.
		std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> m_watchedPresets;

		// When a watched file last changed, in ticks. 0 if none did since the fonts were last asked for.
		uint64_t m_editedAt = 0;
		uint64_t m_lastPoll = 0;

		// The configuration's fonts are read again only once told that they changed.
		bool m_settingsChanged = true;
		FontRequest m_current;
		std::optional<FontRequest> m_requested;
		std::string m_status;

	public:
		PresetController(std::shared_ptr<Config> config, FontLoader& loader);
		PresetController(const PresetController&) = delete;
		PresetController& operator=(const PresetController&) = delete;

		// The configuration's fonts changed, which the next update reads.
		void SettingsChanged() { m_settingsChanged = true; }

		// At the update. Takes what the loader made, and asks it for fonts if the settings they are made from changed or a watched file was
		// edited. Returns the fonts to apply, if any were made since the last call.
		std::unique_ptr<FontSet> Update();

	private:
		void SetStatus(std::string value);

		void Watch(WatchList watch);

		// Marks the time if a watched file changed.
		void PollWatchers();
	};
}
