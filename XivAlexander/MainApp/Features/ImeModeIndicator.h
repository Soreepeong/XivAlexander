#pragma once

namespace XivAlexander::Apps::MainApp::Features {
	/// Makes the IME mode indicator in the chat input follow Korean and Chinese IMEs, and keeps the game from turning on
	/// their full-width mode; the game reads every IME as a Japanese one unless the client language is Korean or Chinese.
	class ImeModeIndicator {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		ImeModeIndicator();
		~ImeModeIndicator();
	};
}
