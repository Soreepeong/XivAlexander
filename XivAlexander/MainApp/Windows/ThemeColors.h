#pragma once
#include <optional>

#include "Utils/Win32/Closeable.h"

namespace XivAlexander {
	enum class ThemeMode : uint8_t;
}

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

	bool IsDarkModeEnabled(ThemeMode mode);

	void ApplyDarkModeToWindow(HWND hWnd, bool dark);

	/// Themed group boxes draw black text regardless, so dark mode leaves them unthemed; checkboxes/radios with text need CustomDrawDarkButton.
	void ApplyDarkModeToControl(HWND hControl, bool dark);

	/// Dark-mode NM_CUSTOMDRAW for checkboxes/radios with text, filled with the parent's WM_CTLCOLORBTN brush; nullopt for anything else.
	std::optional<LRESULT> CustomDrawDarkButton(const NMCUSTOMDRAW& nmcd);

	std::optional<LRESULT> HandleDarkModeWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}
