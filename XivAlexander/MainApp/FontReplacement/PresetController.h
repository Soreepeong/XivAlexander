#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Presets are selected per game font family, which takes faces only from its own presets (the last wins). Loads on its own thread, after settings changes and quiet file changes.
	// Only the presets folder and the glyph image folders are watched: an edit of a preset outside the folder goes unnoticed until another load.
	class PresetController {
	public:
		// Called on the game's thread, between frames.
		using Apply = std::function<void(Presets::Faces faces, bool systemFallback)>;

		// Reads each preset once, by path relative to the presets folder or absolute; unreadable ones are left out, with why in failures.
		class PresetReader {
			const std::filesystem::path m_folder;
			std::vector<std::string>& m_failures;
			std::map<std::string, std::optional<Presets::Faces>, LessIgnoringCase> m_loaded;

		public:
			PresetReader(std::filesystem::path folder, std::vector<std::string>& failures);

			const std::optional<Presets::Faces>& operator()(const std::string& preset);

			[[nodiscard]] size_t ReadCount() const;
		};

		// Later sources win; of a preset, only the family's faces are taken. None if the family is off, so the game's font stays.
		// The settings' font family pages preview with this too, so it touches nothing the replacer uses.
		static Presets::Faces MakeFamilyFaces(const std::string& family, const FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool* usesFont = nullptr);

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

		void ThreadBody();

		// Unreadable presets are left out, unless keepOnFailure: then what was applied stays until they all read.
		void Load(bool keepOnFailure);

		// As FontChanger.Presets.Native's FaceFromFont: each face is the game's glyphs replaced by the font's; none if it isn't installed.
		static std::optional<Presets::Faces> MakeFaces(const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, std::vector<std::string>& failures);
	};
}
