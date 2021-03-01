#include "pch.h"
#include "resource.h"
#include "App_Window_TrayIcon.h"
#include "App_Window_Config.h"

static const auto WmTrayCallback = WM_APP + 1;
static const int TrayItemId = 1;
static const int TimerIdReregisterTrayIcon = 100;

static WNDCLASSEXW WindowClass() {
	Utils::Win32Handle<HICON, DestroyIcon> hIcon(LoadIcon(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)));
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_TRAY_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::Log";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::TrayIcon::TrayIcon(HWND hGameWnd, std::function<void()> unloadFunction)
	: Base(WindowClass(), L"XivAlexander", WS_OVERLAPPEDWINDOW, WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 480, 80, nullptr, nullptr)
	, m_triggerUnload(unloadFunction)
	, m_hGameWnd(hGameWnd)
	, m_uTaskbarRestartMessage(RegisterWindowMessage(TEXT("TaskbarCreated"))) {

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	
	std::vector<unsigned char> hashSourceData{ 0x95, 0xf8, 0x89, 0x5c, 0x59, 0x94, 0x44, 0xf2, 0x9d, 0xda, 0xa6, 0x9a, 0x91, 0xb4, 0xe8, 0x51 };
	hashSourceData.insert(hashSourceData.begin(), reinterpret_cast<unsigned char*>(path), reinterpret_cast<unsigned char*>(path) + sizeof path);
	HashData(hashSourceData.data(), static_cast<DWORD>(hashSourceData.size()), reinterpret_cast<BYTE*>(&m_guid.Data1), static_cast<DWORD>(sizeof GUID));
	
	const auto title = Utils::FormatString(L"XivAlexander(%d): %s", GetCurrentProcessId(), path);
	SetWindowTextW(m_hWnd, title.c_str());
	ModifyMenu(GetMenu(m_hWnd), ID_TRAYMENU_CURRENT_INFO, MF_BYCOMMAND | MF_DISABLED, ID_TRAYMENU_CURRENT_INFO, title.c_str());

	RegisterTrayIcon();

	// Try to restore tray icon every 5 seconds in case things go wrong
	SetTimer(m_hWnd, TimerIdReregisterTrayIcon, 5000, nullptr);

	m_callbackHandle = ConfigRepository::Config().ShowControlWindow.OnChangeListener([this](ConfigItemBase&) {
		ShowWindow(m_hWnd, ConfigRepository::Config().ShowControlWindow ? SW_SHOW : SW_HIDE);
		});
	if (ConfigRepository::Config().ShowControlWindow)
		ShowWindow(m_hWnd, SW_SHOW);
}

App::Window::TrayIcon::~TrayIcon() {
	m_callbackHandle = nullptr;
	Destroy();
}

LRESULT App::Window::TrayIcon::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (!lParam && MessageBoxW(m_hWnd, L"Closing this window will unload XivAlexander. Disable \"Show Control Window\" from the menu to hide this window. Proceed?", L"XivAlexander", MB_YESNO | MB_ICONQUESTION) == IDNO) {
			return 0;
		}
	} else if (uMsg == WM_INITMENUPOPUP) {
		RepopulateMenu(GetMenu(m_hWnd));
	} else if (uMsg == WM_COMMAND) {
		if (!lParam) {
			auto& config = ConfigRepository::Config();
			switch (LOWORD(wParam)) {
				case ID_TRAYMENU_ALWAYSONTOP:
					config.AlwaysOnTop = !config.AlwaysOnTop;
					return 0;

				case ID_TRAYMENU_HIGH_LATENCY_MITIGATION:
					config.UseHighLatencyMitigation = !config.UseHighLatencyMitigation;
					return 0;

				case ID_TRAYMENU_USEIPCTYPEFINDER:
					config.UseOpcodeFinder = !config.UseOpcodeFinder;
					return 0;

				case ID_MENU_USEDELAYDETECTION:
					config.UseAutoAdjustingExtraDelay = !config.UseAutoAdjustingExtraDelay;
					return 0;

				case ID_TRAYMENU_RELOADCONFIGURATION:
					config.Reload();
					return 0;

				case ID_TRAYMENU_SHOWLOGGINGWINDOW:
					config.ShowLoggingWindow = !config.ShowLoggingWindow;
					return 0;

				case ID_TRAYMENU_SHOWCONTROLWINDOW:
					config.ShowControlWindow = !config.ShowControlWindow;
					return 0;

				case ID_TRAYMENU_UNLOADXIVALEXANDER:
					m_triggerUnload();
					return 0;

				case ID_TRAYMENU_EDITCONFIGURATION:
					if (App::Window::Config::m_pConfigWindow && !App::Window::Config::m_pConfigWindow->IsDestroyed()) {
						SetFocus(App::Window::Config::m_pConfigWindow->GetHandle());
					} else {
						App::Window::Config::m_pConfigWindow = std::make_unique< App::Window::Config>();
					}
					return 0;

				case ID_VIEW_ALWAYSONTOP:
				{
					HMENU hMenu = GetMenu(m_hWnd);
					MENUITEMINFOW menuInfo = { sizeof(MENUITEMINFOW) };
					menuInfo.fMask = MIIM_STATE;
					GetMenuItemInfo(hMenu, ID_VIEW_ALWAYSONTOP, FALSE, &menuInfo);
					if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) {
						SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
						menuInfo.fState &= ~MFS_CHECKED;
					} else {
						SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
						menuInfo.fState |= MFS_CHECKED;
					}
					SetMenuItemInfoW(hMenu, ID_VIEW_ALWAYSONTOP, FALSE, &menuInfo);
					return 0;
				}
			}
		}
	} else if (uMsg == WmTrayCallback) {
		const auto iconId = HIWORD(lParam);
		const auto eventId = LOWORD(lParam);
		if (eventId == WM_CONTEXTMENU) {
			HMENU hMenu = GetMenu(m_hWnd);
			HMENU hSubMenu = GetSubMenu(hMenu, 0);
			RepopulateMenu(hSubMenu);
			POINT curPoint;
			GetCursorPos(&curPoint);
			const auto result = TrackPopupMenu(
				GetSubMenu(GetMenu(m_hWnd), 0),
				TPM_RETURNCMD | TPM_NONOTIFY,
				curPoint.x,
				curPoint.y,
				0,
				m_hWnd,
				NULL
			);

			if (result)
				SendMessage(m_hWnd, WM_COMMAND, MAKEWPARAM(result, 0), 0);
		}
	} else if (uMsg == m_uTaskbarRestartMessage) {
		RegisterTrayIcon();
	} else if (uMsg == WM_TIMER) {
		if (wParam == TimerIdReregisterTrayIcon) {
			RegisterTrayIcon();
		}
	}
	return Base::WndProc(uMsg, wParam, lParam);
}

void App::Window::TrayIcon::OnDestroy() {
	m_triggerUnload();

	App::Window::Config::m_pConfigWindow = nullptr;
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.guidItem = m_guid;
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	nid.uFlags = NIF_GUID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	Base::OnDestroy();
	PostQuitMessage(0);
}

void App::Window::TrayIcon::RepopulateMenu(HMENU hMenu) {
	const auto& config = ConfigRepository::Config();

	MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };
	mii.fMask = MIIM_STATE;

	GetMenuItemInfoW(hMenu, ID_VIEW_ALWAYSONTOP, false, &mii);
	if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_VIEW_ALWAYSONTOP, false, &mii);

	GetMenuItemInfoW(hMenu, ID_TRAYMENU_ALWAYSONTOP, false, &mii);
	if (config.AlwaysOnTop)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_TRAYMENU_ALWAYSONTOP, false, &mii);

	GetMenuItemInfoW(hMenu, ID_TRAYMENU_HIGH_LATENCY_MITIGATION, false, &mii);
	if (config.UseHighLatencyMitigation)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_TRAYMENU_HIGH_LATENCY_MITIGATION, false, &mii);

	GetMenuItemInfoW(hMenu, ID_TRAYMENU_SHOWCONTROLWINDOW, false, &mii);
	if (config.ShowControlWindow)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_TRAYMENU_SHOWCONTROLWINDOW, false, &mii);

	GetMenuItemInfoW(hMenu, ID_TRAYMENU_SHOWLOGGINGWINDOW, false, &mii);
	if (config.ShowLoggingWindow)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_TRAYMENU_SHOWLOGGINGWINDOW, false, &mii);

	GetMenuItemInfoW(hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, false, &mii);
	if (config.UseOpcodeFinder)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, false, &mii);

	GetMenuItemInfoW(hMenu, ID_MENU_USEDELAYDETECTION, false, &mii);
	if (config.UseAutoAdjustingExtraDelay)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, ID_MENU_USEDELAYDETECTION, false, &mii);
}

void App::Window::TrayIcon::RegisterTrayIcon() {
	Utils::Win32Handle<HICON, DestroyIcon> hIcon(LoadIcon(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)));
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.uVersion = NOTIFYICON_VERSION_4;
	nid.guidItem = m_guid;
	nid.uID = TrayItemId;
	nid.hWnd = this->m_hWnd;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
	nid.uCallbackMessage = WmTrayCallback;
	nid.hIcon = hIcon;
	wcscpy_s(nid.szTip, Utils::FormatString(L"XivAlexander(%d)", GetCurrentProcessId()).c_str());
	Shell_NotifyIconW(NIM_ADD, &nid);
	Shell_NotifyIconW(NIM_SETVERSION, &nid);
}
