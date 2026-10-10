#pragma once

namespace XivAlexander::Apps::MainApp::Features {
	/// The game reads every IME as Japanese unless the client is Korean or Chinese: fixes the chat IME indicator and full-width mode for those IMEs.
	class ImeModeIndicator {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		ImeModeIndicator();
		~ImeModeIndicator();
	};
}
