#pragma once

#include "Utils/Win32/Handle.h"

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class GamePause {
		Utils::Win32::Event m_resume;
		Utils::Win32::Thread m_staller;

	public:
		explicit GamePause(App& app);
		GamePause(const GamePause&) = delete;
		GamePause& operator=(const GamePause&) = delete;
		~GamePause();
	};
}
