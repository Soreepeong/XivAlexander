#include "pch.h"
#include "Apps/MainApp/Window/ThemeColors.h"

#include "Config.h"

static const XivAlexander::Apps::MainApp::Window::ThemeColors Dark{
	.UseSystemColors = false,
	.Background = RGB(32, 32, 32),
	.Foreground = RGB(220, 220, 220),
	.BackgroundSelection = RGB(58, 88, 128),
	.BackgroundHovered = RGB(51, 51, 51),
	.BackgroundWeak = RGB(45, 45, 45),
	.ForegroundWeak = RGB(100, 100, 100),
	.SciForegroundLogDebug = RGB(120, 120, 120),
	.SciForegroundLogInfo = RGB(200, 200, 200),
	.SciForegroundLogWarning = RGB(240, 200, 0),
	.SciForegroundLogError = RGB(255, 100, 100),
};

static const XivAlexander::Apps::MainApp::Window::ThemeColors Light{
	.UseSystemColors = true,
	.Background = 0,
	.Foreground = 0,
	.BackgroundSelection = 0,
	.BackgroundHovered = 0,
	.BackgroundWeak = RGB(232, 232, 232),
	.ForegroundWeak = RGB(128, 128, 128),
	.SciForegroundLogDebug = RGB(80, 80, 80),
	.SciForegroundLogInfo = RGB(0, 0, 0),
	.SciForegroundLogWarning = RGB(160, 160, 0),
	.SciForegroundLogError = RGB(255, 80, 80),
};

COLORREF XivAlexander::Apps::MainApp::Window::ThemeColors::GetBackground() const {
	return UseSystemColors ? GetSysColor(COLOR_WINDOW) : Background;
}

COLORREF XivAlexander::Apps::MainApp::Window::ThemeColors::GetForeground() const {
	return UseSystemColors ? GetSysColor(COLOR_WINDOWTEXT) : Foreground;
}

COLORREF XivAlexander::Apps::MainApp::Window::ThemeColors::GetBackgroundSelection() const {
	return UseSystemColors ? GetSysColor(COLOR_HIGHLIGHT) : BackgroundSelection;
}

Utils::Win32::Brush XivAlexander::Apps::MainApp::Window::ThemeColors::CreateBackgroundBrush() const {
	return {CreateSolidBrush(GetBackground()), HBRUSH(), "CreateBackgroundBrush failure"};
}

Utils::Win32::Brush XivAlexander::Apps::MainApp::Window::ThemeColors::CreateBackgroundWeakBrush() const {
	return {CreateSolidBrush(BackgroundWeak), HBRUSH(), "CreateBackgroundWeakBrush failure"};
}

void XivAlexander::Apps::MainApp::Window::ThemeColors::ApplyToHDC(HDC hdc) const {
	SetBkColor(hdc, GetBackground());
	SetTextColor(hdc, GetForeground());
}

const XivAlexander::Apps::MainApp::Window::ThemeColors& XivAlexander::Apps::MainApp::Window::GetThemeColors(bool dark) {
	return dark ? Dark : Light;
}

bool XivAlexander::Apps::MainApp::Window::IsSystemDarkModeEnabled() {
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return false;
	DWORD value = 1, size = sizeof(value);
	RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
		reinterpret_cast<LPBYTE>(&value), &size);
	RegCloseKey(hKey);
	return !value;
}

std::optional<LRESULT> XivAlexander::Apps::MainApp::Window::HandleDarkModeWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	// https://stackoverflow.com/questions/77985210/how-to-set-menu-bar-color-in-win32

	constexpr UINT WM_UAHDRAWMENU = 0x0091;
	constexpr UINT WM_UAHDRAWMENUITEM = 0x0092;

	struct UAHMENU {
		HMENU hmenu;
		HDC hdc;
		DWORD dwFlags;
	};
	struct UAHDRAWMENU {
		UAHMENU um;
		HTHEME hTheme;
	};
	struct UAHMENUITEMMETRICS {
		DWORD rgsizeBar[2];
		DWORD rgsizePopup[4];
	};
	struct UAHMENUPOPUPMETRICS {
		DWORD rgcx[4];
		BOOL fUpdateMaxWidths : 2;
	};
	struct UAHMENUITEM {
		int iPosition;
		UAHMENUITEMMETRICS umim;
		UAHMENUPOPUPMETRICS umpm;
	};
	struct UAHDRAWMENUITEM {
		DRAWITEMSTRUCT dis;
		UAHMENU um;
		UAHMENUITEM umi;
	};

	switch (uMsg) {
		case WM_UAHDRAWMENU: {
			const auto& colors = GetThemeColors(true);
			const auto pUDM = reinterpret_cast<UAHDRAWMENU*>(lParam);
			MENUBARINFO mbi{.cbSize = sizeof(mbi)};
			GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);
			RECT rcWindow;
			GetWindowRect(hWnd, &rcWindow);
			RECT rc = mbi.rcBar;
			OffsetRect(&rc, -rcWindow.left, -rcWindow.top);
			rc.bottom += 1;  // cover the 1px separator line drawn by Windows below the menu bar
			const auto hBrush = CreateSolidBrush(colors.GetBackground());
			FillRect(pUDM->um.hdc, &rc, hBrush);
			DeleteObject(hBrush);
			return TRUE;
		}

		case WM_UAHDRAWMENUITEM: {
			const auto& colors = GetThemeColors(true);
			const auto pUDMI = reinterpret_cast<UAHDRAWMENUITEM*>(lParam);

			wchar_t buf[256]{};
			MENUITEMINFOW mii{
				.cbSize = sizeof(mii),
				.fMask = MIIM_STRING,
				.dwTypeData = buf,
				.cch = static_cast<UINT>(std::size(buf) - 1),
			};
			GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

			const bool selected = (pUDMI->dis.itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
			const bool grayed = (pUDMI->dis.itemState & ODS_GRAYED) != 0;

			const auto hBrush = CreateSolidBrush(selected ? colors.BackgroundHovered : colors.Background);
			FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, hBrush);
			DeleteObject(hBrush);

			SetTextColor(pUDMI->um.hdc, grayed ? colors.ForegroundWeak : colors.Foreground);
			SetBkMode(pUDMI->um.hdc, TRANSPARENT);
			DrawTextW(pUDMI->um.hdc, buf, -1, &pUDMI->dis.rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
			return TRUE;
		}

		case WM_NCPAINT:
		case WM_NCACTIVATE: {
			MENUBARINFO mbi = {sizeof(mbi)};
			if (!GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi))
				return std::nullopt;

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			MapWindowPoints(hWnd, nullptr, reinterpret_cast<POINT*>(&rcClient), 2);

			RECT rcWindow;
			GetWindowRect(hWnd, &rcWindow);
			OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);

			// the rcBar is offset by the window rect
			RECT rcAnnoyingLine = rcClient;
			rcAnnoyingLine.bottom = rcAnnoyingLine.top;
			rcAnnoyingLine.top--;

			const auto hdc = GetWindowDC(hWnd);
			FillRect(hdc, &rcAnnoyingLine, Dark.CreateBackgroundWeakBrush());
			ReleaseDC(hWnd, hdc);
			return TRUE;
		}
	}

	return std::nullopt;
}
