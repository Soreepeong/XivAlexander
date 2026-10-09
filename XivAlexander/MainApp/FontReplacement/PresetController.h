#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Presets.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Loads the presets selected from the preset folder (the runtime configuration's FontReplacement) into the
	// replacer, and again whenever the settings, their files or their glyph images change. Presets are selected per game font
	// family: a family's faces come from its presets only (the last one's of a face in several), so a preset of every family
	// can be used for one. Reading and loading happen on a thread of its own, one at a time: right after the settings change,
	// and once changed files have been quiet for a while.
	class PresetController {
	public:
		// Applies faces on the game's thread, between frames.
		using Apply = std::function<void(Presets::Faces faces, bool systemFallback)>;

	private:
		const std::shared_ptr<Config> m_config;
		const Apply m_apply;
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

		// Gets what happened at the last load.
		[[nodiscard]] std::string Status();

		// Loads the presets again at once, as the settings say now.
		void Reload();

	private:
		// Loads after a delay, with whatever else is pending then. A load for files that changed (edited) keeps what was
		// applied if a preset can't be read, as it may be half written.
		void Schedule(bool edited, std::chrono::milliseconds delay);

		void ThreadBody();

		// Reads the selected presets, and applies each family's faces from its presets. Presets that can't be read are left
		// out, unless keepOnFailure: then what was applied stays until they all read.
		void Load(bool keepOnFailure);

		// Makes a family's faces with a system font (FontChanger.Presets.Native's FaceFromFont); none if it isn't installed.
		static std::optional<Presets::Faces> MakeFaces(const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, std::vector<std::string>& failures);
	};
}
