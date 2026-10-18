#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	// The port of FontChanger.DalamudPlugin's Plugin, without its window.
	class FontReplacement {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		// Not on the game's thread, which it waits for.
		explicit FontReplacement(App& app);
		~FontReplacement();
	};
}
