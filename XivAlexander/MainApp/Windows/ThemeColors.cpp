#include "pch.h"
#include "MainApp/Windows/ThemeColors.h"

#include "Utils/Win32/LoadedModule.h"

#include "Config.h"

namespace {
	const XivAlexander::Apps::MainApp::Window::ThemeColors Dark{
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

		.SciForegroundJsonKey = RGB(204, 204, 0),
		.SciForegroundJsonValue = RGB(100, 200, 255),
		.SciForegroundJsonString = RGB(210, 230, 150),
		.SciForegroundJsonNumber = RGB(150, 255, 150),
		.SciForegroundJsonBracket = RGB(255, 150, 100),
		.SciForegroundJsonEscape = RGB(200, 200, 200),
	};

	const XivAlexander::Apps::MainApp::Window::ThemeColors Light{
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

		.SciForegroundJsonKey = RGB(120, 0, 0),
		.SciForegroundJsonValue = RGB(0, 0, 180),
		.SciForegroundJsonString = RGB(0, 100, 0),
		.SciForegroundJsonNumber = RGB(0, 0, 200),
		.SciForegroundJsonBracket = RGB(140, 70, 0),
		.SciForegroundJsonEscape = RGB(100, 50, 120),
	};
}

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

bool XivAlexander::Apps::MainApp::Window::IsDarkModeEnabled(ThemeMode mode) {
	switch (mode) {
		case ThemeMode::Dark:
			return true;
		case ThemeMode::Light:
			return false;
		default:
			return IsSystemDarkModeEnabled();
	}
}

namespace {
	const Utils::Win32::LoadedModule& UxTheme() {
		static const Utils::Win32::LoadedModule uxTheme(GetModuleHandleW(L"uxtheme.dll"), false);
		return uxTheme;
	}

	void AllowDarkModeForWindow(HWND hWnd, bool dark) {
		if (static const auto pAllowDarkModeForWindow = UxTheme().GetProcAddress<bool (WINAPI*)(HWND, bool)>(133, false))
			pAllowDarkModeForWindow(hWnd, dark);
	}
}

void XivAlexander::Apps::MainApp::Window::ApplyDarkModeToWindow(HWND hWnd, bool dark) {
	const BOOL darkBool = dark ? TRUE : FALSE;
	if (FAILED(DwmSetWindowAttribute(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkBool, sizeof(darkBool))))
		(void)DwmSetWindowAttribute(hWnd, 19, &darkBool, sizeof(darkBool));

	if (static const auto pSetPreferredAppMode = UxTheme().GetProcAddress<DWORD (WINAPI*)(DWORD)>(135, false))
		pSetPreferredAppMode(dark ? 1 : 0);

	AllowDarkModeForWindow(hWnd, dark);
	(void)SetWindowTheme(hWnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);

	if (static const auto pFlushMenuThemes = UxTheme().GetProcAddress<void (WINAPI*)()>(136, false))
		pFlushMenuThemes();
}

void XivAlexander::Apps::MainApp::Window::ApplyDarkModeToControl(HWND hControl, bool dark) {
	AllowDarkModeForWindow(hControl, dark);

	wchar_t className[64]{};
	GetClassNameW(hControl, className, static_cast<int>(std::size(className)));
	if (_wcsicmp(className, WC_COMBOBOXW) == 0 || _wcsicmp(className, WC_EDITW) == 0) {
		(void)SetWindowTheme(hControl, dark ? L"DarkMode_CFD" : nullptr, nullptr);
	} else if (_wcsicmp(className, WC_TREEVIEWW) == 0) {
		(void)SetWindowTheme(hControl, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	} else if (_wcsicmp(className, WC_BUTTONW) == 0) {
		if (dark && (GetWindowLongPtrW(hControl, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX && GetWindowTextLengthW(hControl) > 0)
			(void)SetWindowTheme(hControl, L"", L"");
		else
			(void)SetWindowTheme(hControl, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	} else {
		(void)SetWindowTheme(hControl, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	}
	SendMessageW(hControl, WM_THEMECHANGED, 0, 0);
}

std::optional<LRESULT> XivAlexander::Apps::MainApp::Window::CustomDrawDarkButton(const NMCUSTOMDRAW& nmcd) {
	if (nmcd.dwDrawStage != CDDS_PREPAINT)
		return std::nullopt;

	const auto hButton = nmcd.hdr.hwndFrom;
	wchar_t className[16]{};
	GetClassNameW(hButton, className, static_cast<int>(std::size(className)));
	if (_wcsicmp(className, WC_BUTTONW) != 0)
		return std::nullopt;

	if (GetWindowLongPtrW(hButton, GWL_STYLE) & BS_PUSHLIKE)
		return std::nullopt;

	int part;
	switch (GetWindowLongPtrW(hButton, GWL_STYLE) & BS_TYPEMASK) {
		case BS_CHECKBOX:
		case BS_AUTOCHECKBOX:
		case BS_3STATE:
		case BS_AUTO3STATE:
			part = BP_CHECKBOX;
			break;
		case BS_RADIOBUTTON:
		case BS_AUTORADIOBUTTON:
			part = BP_RADIOBUTTON;
			break;
		default:
			return std::nullopt;
	}

	std::wstring text(static_cast<size_t>(GetWindowTextLengthW(hButton)) + 1, L'\0');
	text.resize(GetWindowTextW(hButton, text.data(), static_cast<int>(text.size())));
	if (text.empty())
		return std::nullopt;

	const auto hTheme = OpenThemeData(hButton, L"Button");
	if (!hTheme)
		return std::nullopt;

	// Theme states: unchecked, checked, mixed (checkboxes only), each as normal, hot, pressed, disabled.
	const auto check = Button_GetCheck(hButton);
	const auto enabled = IsWindowEnabled(hButton);
	const auto stateOffset = !enabled ? 3 : (nmcd.uItemState & CDIS_SELECTED) ? 2 : (nmcd.uItemState & CDIS_HOT) ? 1 : 0;
	const auto state = 1 + (check == BST_CHECKED ? 4 : check == BST_INDETERMINATE && part == BP_CHECKBOX ? 8 : 0) + stateOffset;

	const auto hdc = nmcd.hdc;
	RECT rc = nmcd.rc;
	if (const auto hBrush = reinterpret_cast<HBRUSH>(SendMessageW(GetParent(hButton), WM_CTLCOLORBTN, reinterpret_cast<WPARAM>(hdc), reinterpret_cast<LPARAM>(hButton))))
		FillRect(hdc, &rc, hBrush);

	const auto uiState = static_cast<DWORD>(SendMessageW(hButton, WM_QUERYUISTATE, 0, 0));
	const auto hPrevFont = SelectObject(hdc, reinterpret_cast<HFONT>(SendMessageW(hButton, WM_GETFONT, 0, 0)));
	TEXTMETRICW tm{};
	GetTextMetricsW(hdc, &tm);

	// Multiline text starts at the top, leaving 1px for the focus rect, with the glyph beside its first line.
	const auto multiline = (GetWindowLongPtrW(hButton, GWL_STYLE) & BS_MULTILINE) != 0;
	SIZE glyph{};
	GetThemePartSize(hTheme, hdc, part, state, nullptr, TS_DRAW, &glyph);
	const auto glyphTop = multiline ? rc.top + 1 + (static_cast<int>(tm.tmHeight) - glyph.cy) / 2 : rc.top + (rc.bottom - rc.top - glyph.cy) / 2;
	RECT rcGlyph{rc.left, glyphTop, rc.left + glyph.cx, glyphTop + glyph.cy};
	DrawThemeBackground(hTheme, hdc, part, state, &rcGlyph, nullptr);
	CloseThemeData(hTheme);

	const auto& colors = GetThemeColors(true);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, enabled ? colors.GetForeground() : colors.ForegroundWeak);
	RECT rcText{rcGlyph.right + glyph.cx / 3, rc.top, rc.right, rc.bottom};
	if (multiline)
		InflateRect(&rcText, -1, -1);
	const auto format = (multiline ? DT_WORDBREAK | DT_EDITCONTROL : DT_SINGLELINE | DT_VCENTER) | DT_LEFT | ((uiState & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0);
	DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, format);

	if ((nmcd.uItemState & CDIS_FOCUS) && !(uiState & UISF_HIDEFOCUS)) {
		RECT rcFocus = rcText;
		DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcFocus, format | DT_CALCRECT);
		if (!multiline)
			OffsetRect(&rcFocus, 0, (rcText.bottom - rcText.top - (rcFocus.bottom - rcFocus.top)) / 2);
		InflateRect(&rcFocus, 1, 1);
		DrawFocusRect(hdc, &rcFocus);
	}
	SelectObject(hdc, hPrevFont);
	return CDRF_SKIPDEFAULT;
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
