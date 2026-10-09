#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	// Draws the game's text with fonts of FFXIV-FontChanger's presets, or of system fonts, rasterized at the size it is drawn
	// at: FontChanger.DalamudPlugin's Plugin, without its window (its parts are in MainApp/FontReplacement). What it uses is
	// in the runtime configuration (FontReplacement), and changes to it apply as they are made.
	class FontReplacement {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		// Sets it up; not on the game's thread, which it waits for. Throws if this version of the game can't be worked with.
		explicit FontReplacement(App& app);
		~FontReplacement();
	};
}
