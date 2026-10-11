#pragma once

// Shared by the MainWindow.*.cpp parts; not included by MainWindow.h.

namespace XivAlexander::Apps::MainApp::Window {
	enum : UINT {
		WmTrayCallback = WM_APP + 2,
		WmRepopulateMenu,
		WmUncloak,
	};

	inline constexpr int TimerIdReregisterTrayIcon = 100;
	inline constexpr int TimerIdRepaint = 101;
	inline constexpr int TimerIdClearCopiedLaunchCommandLine = 102;
}
