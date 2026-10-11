#pragma once

namespace XivAlexander::Apps::MainApp::Features {
	/// Makes display-only text rules (nouns, line breaking, dialogue boxes) follow a language other than the game's, for text merged from another client.
	class DisplayLanguage {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		DisplayLanguage();
		~DisplayLanguage();

		void Update();

		/// Whether the settings ask for anything other than the game's own rules.
		[[nodiscard]] static bool IsNeeded();
	};
}
