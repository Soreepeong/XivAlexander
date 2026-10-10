#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	// Draws game text with FFXIV-FontChanger presets or system fonts at the drawn size: FontChanger.DalamudPlugin's Plugin, without its window.
	class FontReplacement {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		// Not on the game's thread, which it waits for; throws if this game version is unsupported.
		explicit FontReplacement(App& app);
		~FontReplacement();
	};
}
