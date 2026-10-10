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

	/// Whether windows are drawn dark under the theme setting.
	bool IsDarkModeEnabled(ThemeMode mode);

	/// Draws a top-level window's title bar and menus dark or light.
	void ApplyDarkModeToWindow(HWND hWnd, bool dark);

	/// Gives a child control the dark or the light theme. Themed group boxes draw their text black whatever the color
	/// set, so in dark mode those are drawn unthemed; checkboxes and radio buttons with text are drawn by
	/// CustomDrawDarkButton instead.
	void ApplyDarkModeToControl(HWND hControl, bool dark);

	/// For NM_CUSTOMDRAW from a checkbox or a radio button with text, in dark mode: draws its themed box and its text in
	/// the dark theme's color, and gives the result to return; nullopt for others, drawn as usual. The background is the
	/// brush the parent gives for WM_CTLCOLORBTN.
	std::optional<LRESULT> CustomDrawDarkButton(const NMCUSTOMDRAW& nmcd);

	std::optional<LRESULT> HandleDarkModeWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}
