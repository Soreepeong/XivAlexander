#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Each game font family takes fonts only from its own sources; loading runs on the controller's own thread.
	class PresetController {
	public:
		// Called on the controller's thread.
		using Apply = std::function<void(Presets::Fonts fonts, bool systemFallback)>;

	private:
		// Unreadable presets read as nullopt, with why in failures.
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
		std::thread m_thread;

	public:
		PresetController(std::shared_ptr<Config> config, Apply apply);
		PresetController(const PresetController&) = delete;
		PresetController& operator=(const PresetController&) = delete;
		~PresetController();

		void Reload();

	private:
		// An edited (file change) load keeps what was applied if a preset can't be read, as it may be half written.
		void Schedule(bool edited, std::chrono::milliseconds delay);

		// Loads run only on this thread, so they never overlap.
		void ThreadBody();

		void RunPending();

		// Unreadable presets are left out, unless keepOnFailure: then what was applied stays until they all read.
		void Load(bool keepOnFailure);

		// Empty if the family is off, so that the game's font stays.
		static Presets::Fonts MakeFamilyFonts(const std::string& family, const FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool& usesFont);

		// The fonts are made to go under those of the sources above, which are listed top first.
		static std::optional<Presets::Fonts> MakeFonts(
			const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, const FontReplacementAdjustments& adjustments, const std::vector<const Presets::Fonts*>& above,
			std::vector<std::string>& failures);
	};
}
