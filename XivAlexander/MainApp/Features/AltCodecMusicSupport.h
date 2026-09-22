#pragma once

#include <memory>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	class AltCodecMusicSupport {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		explicit AltCodecMusicSupport(App& app);
		~AltCodecMusicSupport();

		void Enable();
		void Disable();
	};
}
