#pragma once

#include <memory>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Misc {
	class OpcodeGuesser {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		explicit OpcodeGuesser(Apps::MainApp::App& app);
		~OpcodeGuesser();

		OpcodeGuesser(const OpcodeGuesser&) = delete;
		OpcodeGuesser& operator=(const OpcodeGuesser&) = delete;
	};
}
