#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class AllIpcMessageLogger {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		AllIpcMessageLogger(App& app);
		~AllIpcMessageLogger();
	};
}
