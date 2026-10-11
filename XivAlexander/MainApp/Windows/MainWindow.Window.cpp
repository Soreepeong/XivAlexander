#include "pch.h"
#include "MainApp/Windows/MainWindow.h"
#include "MainApp/Windows/MainWindow.Internal.h"
#include "MainApp/Windows/ThemeColors.h"

#include "Utils/Win32/Resource.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"
#include "MainApp/Features/LoginSessions.h"
#include "MainApp/Features/MainThreadTimingHandler.h"
#include "MainApp/Features/SocketHook.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Windows/ConfigWindow.h"
#include "MainApp/Windows/SettingsView.h"
#include "MainApp/Windows/ProgressPopupWindow.h"

namespace {
	constexpr int TrayItemId = 0x4c19fd7a;
}

LRESULT XivAlexander::Apps::MainApp::Window::MainWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (lParam == 1) {
			DestroyWindow(m_hWnd);
		} else if (lParam == 2) {
			RemoveTrayIcon();
			TerminateProcess(GetCurrentProcess(), static_cast<UINT>(wParam));
		} else {
			HideToTray();
		}
		return 0;
	} else if (uMsg == WM_NCHITTEST) {
		auto res = BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
		if (res == HTCLIENT)
			res = HTCAPTION;
		return res;
	} else if (uMsg == WM_INITMENUPOPUP) {
		SetMenuStates();
	} else if (uMsg == WM_DROPFILES) {
		const auto hDrop = reinterpret_cast<HDROP>(wParam);
		std::vector<std::filesystem::path> paths;
		for (UINT i = 0, i_ = DragQueryFileW(hDrop, UINT_MAX, nullptr, 0); i < i_; ++i) {
			std::wstring buf(static_cast<size_t>(1) + DragQueryFileW(hDrop, i, nullptr, 0), L'\0');
			buf.resize(DragQueryFileW(hDrop, i, buf.data(), static_cast<UINT>(buf.size())));
			paths.emplace_back(std::move(buf));
		}

		InstallMultipleFiles(paths);

	} else if (uMsg == Features::LoginSessions::ReloadMessage) {
		const auto loginSessions = m_app.GetLoginSessions();
		if (!loginSessions)
			return 0;
		loginSessions->Reload(wParam == 1);
		return 1;

	} else if (uMsg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
		// From the dialog keys: Enter applies what is being typed in the settings.
		if (LOWORD(wParam) == IDOK && m_settingsView)
			m_settingsView->CommitTypedText();
		return 0;
	} else if (uMsg == WM_GETMINMAXINFO) {
		const auto zoom = GetZoom();
		reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {static_cast<LONG>(480 * zoom), static_cast<LONG>(320 * zoom)};
		return 0;
	} else if (uMsg == WM_COMMAND) {
		if (!lParam) {
			try {
				const auto menuId = LOWORD(wParam);
				OnCommand_Menu_File(menuId);
				OnCommand_Menu_Restart(menuId);
				OnCommand_Menu_Network(menuId);
				OnCommand_Menu_Modding(menuId);
				OnCommand_Menu_Configure(menuId);
				OnCommand_Menu_View(menuId);
				OnCommand_Menu_Help(menuId);
			} catch (const std::exception& e) {
				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			}
			return 0;
		}
	} else if (uMsg == WmTrayCallback) {
		const auto eventId = LOWORD(lParam);
		if (eventId == WM_CONTEXTMENU) {
			ShowContextMenu();
		} else if (eventId == WM_LBUTTONUP) {
			const auto now = GetTickCount64();
			if (m_lastTrayIconLeftButtonUp + GetDoubleClickTime() > now) {
				if (m_config->Runtime.Ui.MainWindow.Show.Toggle())
					SetForegroundWindow(m_hWnd);
				m_lastTrayIconLeftButtonUp = 0;
			} else
				m_lastTrayIconLeftButtonUp = now;
		}
	} else if (uMsg == WmRepopulateMenu) {
		RepopulateMenu();
	} else if (uMsg == WmUncloak) {
		// Painted before it is shown, so that it never appears blank.
		RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
		BOOL cloak = FALSE;
		DwmSetWindowAttribute(m_hWnd, DWMWA_CLOAK, &cloak, sizeof cloak);
	} else if (uMsg == m_uTaskbarRestartMessage) {
		RegisterTrayIcon();
	} else if (uMsg == WM_TIMER) {
		if (wParam == TimerIdReregisterTrayIcon) {
			RegisterTrayIcon();
		} else if (wParam == TimerIdRepaint) {
			m_settingsView->InvalidateStatus();
		} else if (wParam == TimerIdClearCopiedLaunchCommandLine) {
			ClearCopiedLaunchCommandLine();
		}
	} else if (uMsg == WM_SIZE) {
		if (wParam == SIZE_MINIMIZED && m_config->Runtime.Ui.MainWindow.HideOnMinimize) {
			HideToTray();
			return 0;
		}
	}
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnLayout(double zoom, double width, double height, int resizeType) {
	if (m_settingsView)
		SetWindowPos(m_settingsView->Handle(), nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), SWP_NOZORDER | SWP_NOACTIVATE);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnDestroy() {
	ClearCopiedLaunchCommandLine();
	m_triggerUnload();

	if (const auto replaceMusicsProgressWindow = decltype(m_backgroundWorkerProgressWindow)(m_backgroundWorkerProgressWindow))
		replaceMusicsProgressWindow->Cancel();
	if (const auto replaceMusicsThread = decltype(m_backgroundWorkerThread)(m_backgroundWorkerThread))
		replaceMusicsThread.Wait();

	m_runtimeConfigEditor.reset();
	m_gameConfigEditor.reset();
	m_settingsView.reset();
	RemoveTrayIcon();
	BaseWindow::OnDestroy();
	PostQuitMessage(0);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnThemeChanged() {
	BaseWindow::OnThemeChanged();
	if (m_settingsView)
		m_settingsView->ApplyTheme();
}

void XivAlexander::Apps::MainApp::Window::MainWindow::PaintStatus(HDC hdc, const RECT& rect) const {
	NONCLIENTMETRICSW ncm{.cbSize = sizeof ncm};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
	const auto hFont = CreateFontIndirectW(&ncm.lfMessageFont);
	const auto hPrevFont = SelectObject(hdc, hFont);
	GetThemeColors(IsDarkModeEnabled()).ApplyToHDC(hdc);

	std::wstring str;
	try {
		const auto window = Utils::QpcUs() - 1000000;
		uint64_t msgPumpMean{}, msgPumpDev{};
		double msgPumpCount{};
		if (const auto handler = m_app.GetMainThreadTimingHelper()) {
			std::tie(msgPumpMean, msgPumpDev) = handler->GetMessagePumpIntervalTrackerUs().MeanAndDeviation(window);
			msgPumpCount = handler->GetMessagePumpIntervalTrackerUs().CountFractional(window);
		}
		// The profile whose choices are used, as the library picks it: the active one, or else the first.
		std::wstring ttmpProfile;
		if (m_app.GetResourceOverrider().IsActive()) {
			const auto& profiles = m_config->Runtime.Modding.Ttmp.ChoicesFiles.Value();
			auto it = std::ranges::find_if(profiles, [](const auto& profile) { return profile.Active && !profile.FileName.empty(); });
			if (it == profiles.end())
				it = std::ranges::find_if(profiles, [](const auto& profile) { return !profile.FileName.empty(); });
			// The default profile's name is shown in the UI's language.
			if (it != profiles.end())
				ttmpProfile = m_config->Runtime.FormatStringRes(IDS_STATUS_TTMPPROFILE, it->Name == ChoicesProfile::DefaultName
					? std::wstring(m_config->Runtime.GetStringRes(IDS_SETTINGS_CHOICES_DEFAULTNAME))
					: xivres::util::unicode::convert<std::wstring>(it->Name.empty() ? it->FileName : it->Name));
		}
		str = m_config->Runtime.FormatStringRes(IDS_MAIN_TEXT,
			msgPumpMean, msgPumpDev, msgPumpCount,
			ttmpProfile,
			m_app.GetSocketHook().Describe());
		if (m_config->Runtime.AreVersionSensitiveFeaturesDisabledTemporarily())
			str = std::format(L"{}\n{}", m_config->Runtime.GetStringRes(IDS_VERSIONSENSITIVE_DISABLEDUNTILRESTART), str);
	} catch (...) {
	}

	const auto pad = static_cast<int>(8 * GetZoom());
	RECT rct = {
		.left = rect.left + pad,
		.top = rect.top + pad,
		.right = rect.right - pad,
		.bottom = rect.bottom - pad,
	};
	DrawTextW(hdc, str.data(), -1, &rct, DT_TOP | DT_LEFT | DT_NOCLIP | DT_EDITCONTROL | DT_WORDBREAK);

	DeleteObject(SelectObject(hdc, hPrevFont));
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RegisterTrayIcon() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"LoadIconW");
	NOTIFYICONDATAW nid{.cbSize = sizeof nid};
	nid.uVersion = NOTIFYICON_VERSION_4;
	nid.uID = TrayItemId;
	nid.hWnd = this->m_hWnd;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid.uCallbackMessage = WmTrayCallback;
	nid.hIcon = hIcon;
	wcscpy_s(nid.szTip, std::format(L"XivAlexander({})", GetCurrentProcessId()).c_str());
	Shell_NotifyIconW(NIM_ADD, &nid);
	Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::HideToTray() {
	auto& config = m_config->Runtime.Ui.MainWindow;
	if (!config.Show)
		return;
	config.Show = false;
	if (!config.NotifyWhenHidden)
		return;

	NOTIFYICONDATAW nid{.cbSize = sizeof nid};
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	nid.uFlags = NIF_INFO;
	nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
	wcsncpy_s(nid.szInfoTitle, m_config->Runtime.GetStringRes(IDS_APP_NAME), _TRUNCATE);
	wcsncpy_s(nid.szInfo, m_config->Runtime.GetStringRes(IDS_TRAY_HIDDEN_NOTIFICATION), _TRUNCATE);
	Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RemoveTrayIcon() {
	NOTIFYICONDATAW nid{.cbSize = sizeof nid};
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}
