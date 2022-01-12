#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class EffectApplicationDelayLogger {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		EffectApplicationDelayLogger(App& app);
		~EffectApplicationDelayLogger();
	};
}
