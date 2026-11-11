#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/Shaders/EdgeShader.h"
#include "MainApp/FontReplacement/Fonts/Shaders/SubpixelShaders.h"
#include "MainApp/FontReplacement/PluginInterface/SubpixelSettingsSource.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontSetLoader;
	class FontReplacer;
	class LineBreaker;
	class NamePlateText;
	class FontSourceEditor;

	// Calls the parts in order at the point of a frame the replacement owns, and passes on what each reports.
	// OnUpdate and then OnPresent run before the game presents a frame. OnUpdate does settings, fonts and resets, then what keeps laid out text.
	// Hooks run in the game's update and draw between frames, and record in a FrameState what others must react to. OnPresent does the atlases
	// and the caches.
	class FrameCoordinator {
	public:
		// Read from the configuration on the game's thread when it changes.
		struct Settings {
			FontReplacementAtlasConfig Atlas;
			FontReplacementEdgeConfig Edge;
			FontReplacementSubpixelMode Subpixel{};
			FontReplacementClearTypeConfig ClearType;
			FontReplacementVerticalSubpixelRenderer VerticalSubpixel{};
			FontReplacementNamePlateMode NamePlates{};
		};

	private:
		FrameState m_state;
		SubpixelShaders m_subpixel{m_state};
		SubpixelSettingsSource m_subpixelSource;

		// What subpixel rendering was last applied with, such as the system's ClearType settings.
		SubpixelShaders::SettingsRecord m_appliedSubpixel;
		EdgeShader m_edge;
		std::unique_ptr<FontReplacer> m_replacer;

		// Null if unsupported by this game version: text is still replaced, but nameplates are as the game bakes them and words split by its
		// rules.
		std::unique_ptr<NamePlateText> m_namePlates;
		std::unique_ptr<LineBreaker> m_lineBreaker;

		std::unique_ptr<FontSetLoader> m_loader;
		std::unique_ptr<FontSourceEditor> m_presets;

	public:
		// Game thread. Throws if fonts can't be replaced in this game version.
		FrameCoordinator(std::shared_ptr<Config> config, const Settings& settings, HWND window);
		FrameCoordinator(const FrameCoordinator&) = delete;
		FrameCoordinator& operator=(const FrameCoordinator&) = delete;

		// Game thread, between frames.
		~FrameCoordinator();

		[[nodiscard]] FontSourceEditor& Presets() const { return *m_presets; }

		// Applies the settings, each part skipping what didn't change.
		void OnUpdate(const Settings& settings, HWND window);

		void OnPresent();

	private:
		// What keeps laid out text lays it out again.
		void RedoText();

		// Everything but the replacer's glyphs and copies, which the game may still use.
		void Unhook();
	};
}
