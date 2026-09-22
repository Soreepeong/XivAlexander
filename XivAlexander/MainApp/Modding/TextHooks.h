#pragma once

#include <memory>

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class TextHooks {
	public:
		TextHooks();
		~TextHooks();

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;
	};
}
