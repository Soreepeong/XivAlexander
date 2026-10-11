#pragma once

#include <memory>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	/// Keeps ExcelMerger's merged text out of what the game sends: chat links are changed back; anything left holds the connection until the user decides.
	class OutgoingText {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		/// The chat rewrite hooks the game, so it is left out when version-sensitive features are not allowed; the network check is not.
		OutgoingText(App& app, bool rewriteChat);
		~OutgoingText();
	};
}
