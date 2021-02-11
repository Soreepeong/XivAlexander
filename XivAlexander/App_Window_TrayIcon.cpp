#include "pch.h"
#include "resource.h"
#include "App_Window_TrayIcon.h"

static const auto WM_TRAY_CALLBACK = WM_APP + 1;
static const int trayItemId = 1;

static WNDCLASSEXW WindowClass() {
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = L"XivAlexander::Window::Log";
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	return wcex;
}

App::Window::TrayIcon::TrayIcon(HWND hGameWnd, std::function<void()> unloadFunction)
	: Base(WindowClass(), L"TrayIcon", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr)
	, m_hMenu(LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_TRAY_MENU)))
	, m_triggerUnload(unloadFunction)
	, m_hGameWnd(hGameWnd) {

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	
	std::vector<unsigned char> hashSourceData{ 0x95, 0xf8, 0x89, 0x5c, 0x59, 0x94, 0x44, 0xf2, 0x9d, 0xda, 0xa6, 0x9a, 0x91, 0xb4, 0xe8, 0x51 };
	hashSourceData.insert(hashSourceData.begin(), reinterpret_cast<unsigned char*>(path), reinterpret_cast<unsigned char*>(path) + sizeof path);
	HashData(hashSourceData.data(), static_cast<DWORD>(hashSourceData.size()), reinterpret_cast<BYTE*>(&m_guid.Data1), static_cast<DWORD>(sizeof GUID));

	Utils::Win32Handle<HICON, DestroyIcon> hIcon(LoadIcon(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)));
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.uVersion = NOTIFYICON_VERSION_4;
	nid.guidItem = m_guid;
	nid.uID = trayItemId;
	nid.hWnd = this->m_hWnd;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
	nid.uCallbackMessage = WM_TRAY_CALLBACK;
	nid.hIcon = hIcon;

	ModifyMenu(m_hMenu, ID_TRAYMENU_CURRENT_INFO, MF_BYCOMMAND | MF_DISABLED, ID_TRAYMENU_CURRENT_INFO, Utils::FormatString(L"XivAlexander(%d): %s", GetCurrentProcessId(), path).c_str());

	wcscpy_s(nid.szTip, Utils::FormatString(L"XivAlexander(%d)", GetCurrentProcessId()).c_str());
	Shell_NotifyIconW(NIM_ADD, &nid);
	Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

App::Window::TrayIcon::~TrayIcon() {
	Destroy();
}

LRESULT App::Window::TrayIcon::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_TRAY_CALLBACK) {
		const auto iconId = HIWORD(lParam);
		const auto eventId = LOWORD(lParam);
		if (eventId == WM_CONTEXTMENU) {
			auto& config = ConfigRepository::Config();
			POINT curPoint;
			GetCursorPos(&curPoint);
			SetForegroundWindow(m_hGameWnd);

			MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };
			mii.fMask = MIIM_STATE;
			GetMenuItemInfoW(m_hMenu, ID_TRAYMENU_ALWAYSONTOP, false, &mii);
			if (config.AlwaysOnTop)
				mii.fState |= MFS_CHECKED;
			else
				mii.fState &= ~MFS_CHECKED;
			SetMenuItemInfoW(m_hMenu, ID_TRAYMENU_ALWAYSONTOP, false, &mii);

			GetMenuItemInfoW(m_hMenu, ID_TRAYMENU_HIGH_LATENCY_MITIGATION, false, &mii);
			if (config.UseHighLatencyMitigation)
				mii.fState |= MFS_CHECKED;
			else
				mii.fState &= ~MFS_CHECKED;
			SetMenuItemInfoW(m_hMenu, ID_TRAYMENU_HIGH_LATENCY_MITIGATION, false, &mii);

			GetMenuItemInfoW(m_hMenu, ID_TRAYMENU_SHOWLOGGINGWINDOW, false, &mii);
			if (config.ShowLoggingWindow)
				mii.fState |= MFS_CHECKED;
			else
				mii.fState &= ~MFS_CHECKED;
			SetMenuItemInfoW(m_hMenu, ID_TRAYMENU_SHOWLOGGINGWINDOW, false, &mii);

			GetMenuItemInfoW(m_hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, false, &mii);
			if (config.UseOpcodeFinder)
				mii.fState |= MFS_CHECKED;
			else
				mii.fState &= ~MFS_CHECKED;
			SetMenuItemInfoW(m_hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, false, &mii);

			switch (TrackPopupMenu(
				GetSubMenu(m_hMenu, 0),
				TPM_RETURNCMD | TPM_NONOTIFY,
				curPoint.x,
				curPoint.y,
				0,
				m_hGameWnd,
				NULL
			)) {
				case ID_TRAYMENU_ALWAYSONTOP:
					config.AlwaysOnTop = !config.AlwaysOnTop;
					break;
				case ID_TRAYMENU_HIGH_LATENCY_MITIGATION:
					config.UseHighLatencyMitigation = !config.UseHighLatencyMitigation;
					break;
				case ID_TRAYMENU_USEIPCTYPEFINDER:
					config.UseOpcodeFinder = !config.UseOpcodeFinder;
					break;
				case ID_TRAYMENU_RELOADCONFIGURATION:
					config.Reload();
					break;
				case ID_TRAYMENU_SHOWLOGGINGWINDOW:
					config.ShowLoggingWindow = !config.ShowLoggingWindow;
					break;
				case ID_TRAYMENU_UNLOADXIVALEXANDER:
					m_triggerUnload();
					break;
			}
		}
	}
	return Base::WndProc(uMsg, wParam, lParam);
}

void App::Window::TrayIcon::OnDestroy() {
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.guidItem = m_guid;
	nid.uID = trayItemId;
	nid.hWnd = m_hWnd;
	nid.uFlags = NIF_GUID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	Base::OnDestroy();
}
