#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Presets are selected per game font family, which takes fonts only from its own presets (the last wins). Loads on its own thread, after settings changes and quiet file changes.
	class PresetController {
	public:
		// Called on the game's thread, between frames.
		using Apply = std::function<void(Presets::Fonts fonts, bool systemFallback)>;

		// Reads each preset once, by path relative to the presets folder or absolute; unreadable ones are left out, with why in failures.
		class PresetReader {
			const std::filesystem::path m_folder;
			std::vector<std::string>& m_failures;
			std::map<std::string, std::optional<Presets::Fonts>, LessIgnoringCase> m_loaded;
			std::vector<std::filesystem::path> m_paths;

		public:
			PresetReader(std::filesystem::path folder, std::vector<std::string>& failures);

			const std::optional<Presets::Fonts>& operator()(const std::string& preset);

			[[nodiscard]] size_t ReadCount() const;

			// Of every preset asked for, read or not.
			[[nodiscard]] const std::vector<std::filesystem::path>& Paths() const { return m_paths; }
		};

		// Later sources win; of a preset, only the family's fonts are taken. None if the family is off, so the game's font stays.
		static Presets::Fonts MakeFamilyFonts(const std::string& family, const FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool* usesFont = nullptr);

	private:
		const std::shared_ptr<Config> m_config;
		const Apply m_apply;
		const std::filesystem::path m_presetFolder;
		std::mutex m_mutex;
		std::condition_variable m_wake;
		bool m_stop = false;
		bool m_pendingLoad = false;
		bool m_pendingSettingsLoad = false;
		std::optional<std::chrono::steady_clock::time_point> m_due;
		std::vector<std::filesystem::path> m_glyphFolders;
		// Presets outside the presets folder, by when they were last written as of the last load (nullopt if missing then).
		std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> m_externalPresets;
		std::string m_status;
		std::thread m_thread;

	public:
		PresetController(std::shared_ptr<Config> config, Apply apply);
		PresetController(const PresetController&) = delete;
		PresetController& operator=(const PresetController&) = delete;
		~PresetController();

		[[nodiscard]] std::string Status();

		void Reload();

	private:
		// An edited (file change) load keeps what was applied if a preset can't be read, as it may be half written.
		void Schedule(bool edited, std::chrono::milliseconds delay);

		// PresetController.Watching.cpp: watches the presets folder, the presets used outside it and their glyph image folders, and runs due loads.
		void ThreadBody();

		// Loads if one is due; on the controller's thread.
		void RunPending();

		// nullopt if it is missing.
		[[nodiscard]] static std::optional<std::filesystem::file_time_type> LastWriteTime(const std::filesystem::path& path);

		// Unreadable presets are left out, unless keepOnFailure: then what was applied stays until they all read.
		void Load(bool keepOnFailure);

		[[nodiscard]] static FontChanger::Structs::LookupStruct ToLookup(const FontReplacementFamilyFont& font);

		// As FontChanger.Presets.Native's FontsFromSystemFont: each font is the game's glyphs replaced by the system font's, under the fonts of the sources
		// above it (the one on top first), as drawn: its scripts of CJK sized as the first of them that has the capitals draws them; changed by the adjustments. None if it isn't installed.
		static std::optional<Presets::Fonts> MakeFonts(
			const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, const FontReplacementAdjustments& adjustments, const std::vector<const Presets::Fonts*>& above,
			std::vector<std::string>& failures);
	};
}
