#pragma once
#include <optional>

#include "XivAlexanderCommon/Utils/Win32/Closeable.h"

namespace XivAlexander::Apps::MainApp::Window {
	struct ThemeColors {
		bool UseSystemColors;

		COLORREF Background;
		COLORREF Foreground;
		COLORREF BackgroundSelection;
		COLORREF BackgroundHovered;
		COLORREF BackgroundWeak;
		COLORREF ForegroundWeak;
		COLORREF SciForegroundLogDebug;
		COLORREF SciForegroundLogInfo;
		COLORREF SciForegroundLogWarning;
		COLORREF SciForegroundLogError;

		COLORREF SciForegroundJsonKey;
		COLORREF SciForegroundJsonValue;
		COLORREF SciForegroundJsonString;
		COLORREF SciForegroundJsonNumber;
		COLORREF SciForegroundJsonBracket;
		COLORREF SciForegroundJsonEscape;

		COLORREF GetBackground() const;
		COLORREF GetForeground() const;
		COLORREF GetBackgroundSelection() const;

		Utils::Win32::Brush CreateBackgroundBrush() const;
		Utils::Win32::Brush CreateBackgroundWeakBrush() const;

		void ApplyToHDC(HDC hdc) const;
	};

	const ThemeColors& GetThemeColors(bool dark);
	
	bool IsSystemDarkModeEnabled();

	std::optional<LRESULT> HandleDarkModeWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}
