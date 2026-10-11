#include "pch.h"
#include "LoaderUi.h"

#include "Utils/Win32/Resource.h"

#include "MainApp/Windows/ThemeColors.h"
#include "XivAlexander.h"

namespace Window = XivAlexander::Apps::MainApp::Window;

LPCWSTR XivAlexander::LoaderApp::Ui::Strings::operator()(UINT id) const {
	return Utils::Win32::FindStringResourceEx(Dll::Module(), id, m_langId) + 1;
}

const DLGTEMPLATE* XivAlexander::LoaderApp::Ui::LoadDialogTemplate(UINT id, WORD langId) {
	// Resources stay mapped for the module's lifetime; freeing the handle does nothing.
	return Utils::Win32::GlobalResource(Dll::Module(), RT_DIALOG, MAKEINTRESOURCEW(id), langId).GetDataAs<DLGTEMPLATE>();
}

void XivAlexander::LoaderApp::Ui::DialogTheme::Apply(HWND hDlg, bool dark) {
	m_dark = dark;
	m_brush = Window::GetThemeColors(dark).CreateBackgroundBrush();
	Window::ApplyDarkModeToWindow(hDlg, dark);
	EnumChildWindows(hDlg, [](HWND hChild, LPARAM lParam) {
		Window::ApplyDarkModeToControl(hChild, lParam != 0);
		return TRUE;
	}, dark ? 1 : 0);
	RedrawWindow(hDlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

std::optional<INT_PTR> XivAlexander::LoaderApp::Ui::DialogTheme::HandleMessage(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) const {
	switch (uMsg) {
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN: {
			if (!m_dark)
				return std::nullopt;
			const auto& colors = Window::GetThemeColors(true);
			const auto hdc = reinterpret_cast<HDC>(wParam);
			SetTextColor(hdc, IsWindowEnabled(reinterpret_cast<HWND>(lParam)) ? colors.GetForeground() : colors.ForegroundWeak);
			SetBkColor(hdc, colors.GetBackground());
			return reinterpret_cast<INT_PTR>(static_cast<HBRUSH>(m_brush));
		}

		case WM_NOTIFY: {
			// Themed checkboxes draw their text black; in dark mode, it is drawn here.
			if (m_dark && reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW) {
				if (const auto result = Window::CustomDrawDarkButton(*reinterpret_cast<LPNMCUSTOMDRAW>(lParam))) {
					SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, *result);
					return TRUE;
				}
			}
			return std::nullopt;
		}
	}
	return std::nullopt;
}

void XivAlexander::LoaderApp::Ui::CenterOnMonitorOf(HWND hWnd, HWND hReference) {
	RECT rcReference{};
	MONITORINFO mi{.cbSize = sizeof mi};
	if (hReference && IsWindowVisible(hReference)) {
		GetWindowRect(hReference, &rcReference);
		GetMonitorInfoW(MonitorFromRect(&rcReference, MONITOR_DEFAULTTONEAREST), &mi);
	} else {
		POINT pt{};
		GetCursorPos(&pt);
		GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
		rcReference = mi.rcWork;
	}

	RECT rc{};
	GetWindowRect(hWnd, &rc);
	const auto width = rc.right - rc.left;
	const auto height = rc.bottom - rc.top;
	auto left = (rcReference.left + rcReference.right - width) / 2;
	auto top = (rcReference.top + rcReference.bottom - height) / 2;
	left = std::max(mi.rcWork.left, std::min(left, mi.rcWork.right - width));
	top = std::max(mi.rcWork.top, std::min(top, mi.rcWork.bottom - height));
	SetWindowPos(hWnd, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int XivAlexander::LoaderApp::Ui::MeasureTextWidth(HWND hControl) {
	auto text = Utils::Win32::GetWindowString(hControl);
	std::erase(text, L'&');
	const auto hdc = GetDC(hControl);
	const auto hPrevFont = SelectObject(hdc, reinterpret_cast<HFONT>(SendMessageW(hControl, WM_GETFONT, 0, 0)));
	SIZE size{};
	GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
	SelectObject(hdc, hPrevFont);
	ReleaseDC(hControl, hdc);
	return size.cx;
}

std::wstring XivAlexander::LoaderApp::Ui::FormatSize(uint64_t bytes) {
	wchar_t buf[64]{};
	if (FAILED(StrFormatByteSizeEx(bytes, SFBS_FLAGS_TRUNCATE_UNDISPLAYED_DECIMAL_DIGITS, buf, static_cast<UINT>(std::size(buf)))))
		return std::format(L"{} B", bytes);
	return buf;
}

UINT XivAlexander::LoaderApp::Ui::TrackMenuUnder(HWND hButton, HMENU hMenu) {
	RECT rc{};
	GetWindowRect(hButton, &rc);
	TPMPARAMS params{.cbSize = sizeof params, .rcExclude = rc};
	return static_cast<UINT>(TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_VERTICAL, rc.left, rc.bottom, GetParent(hButton), &params));
}
