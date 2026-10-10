#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class PatchCode {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		PatchCode(App& app);
		~PatchCode();
	};
}
