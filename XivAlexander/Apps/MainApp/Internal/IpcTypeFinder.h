#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class IpcTypeFinder {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		IpcTypeFinder(App& app);
		~IpcTypeFinder();
	};
}
