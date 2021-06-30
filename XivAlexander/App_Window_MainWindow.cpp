#include "pch.h"
#include "resource.h"
#include "App_App.h"
#include "App_Window_ConfigWindow.h"
#include "App_Window_MainWindow.h"
#include "App_Network_SocketHook.h"

static const auto WmTrayCallback = WM_APP + 1;
static const int TrayItemId = 1;
static const int TimerIdReregisterTrayIcon = 100;
static const int TimerIdRepaint = 101;

static WNDCLASSEXW WindowClass() {
	const auto hIcon = Utils::Win32::Closeable::Icon(LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"Failed to load app icon.");
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_TRAY_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::Main";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::Main::Main(HWND hGameWnd, std::function<void()> unloadFunction)
	: BaseWindow(WindowClass(), L"XivAlexander", WS_OVERLAPPEDWINDOW, WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 480, 160, nullptr, nullptr)
	, m_hGameWnd(hGameWnd)
	, m_triggerUnload(std::move(unloadFunction))
	, m_uTaskbarRestartMessage(RegisterWindowMessage(TEXT("TaskbarCreated")))
	, m_path(Utils::Win32::Modules::PathFromModule()) {

	std::tie(m_sRegion, m_sVersion) = XivAlex::ResolveGameReleaseRegion();
	
	{
		std::vector<uint8_t> hashSourceData{ 0x95, 0xf8, 0x89, 0x5c, 0x59, 0x94, 0x44, 0xf2, 0x9d, 0xda, 0xa6, 0x9a, 0x91, 0xb4, 0xe8, 0x51 };
		auto buf = m_path.wstring();
		hashSourceData.insert(hashSourceData.begin(), reinterpret_cast<const uint8_t*>(&buf[0]), reinterpret_cast<const uint8_t*>(&buf[buf.size()]));
		HashData(hashSourceData.data(), static_cast<DWORD>(hashSourceData.size()), reinterpret_cast<BYTE*>(&m_guid.Data1), static_cast<DWORD>(sizeof GUID));
	}
	
	const auto title = std::format(L"XivAlexander: {}, {}, {}", GetCurrentProcessId(), m_sRegion, m_sVersion);
	SetWindowTextW(m_hWnd, title.c_str());
	ModifyMenu(GetMenu(m_hWnd), ID_TRAYMENU_CURRENTINFO, MF_BYCOMMAND | MF_DISABLED, ID_TRAYMENU_CURRENTINFO, title.c_str());

	RegisterTrayIcon();

	// Try to restore tray icon every 5 seconds in case things go wrong
	SetTimer(m_hWnd, TimerIdReregisterTrayIcon, 5000, nullptr);

	SetTimer(m_hWnd, TimerIdRepaint, 1000, nullptr);

	m_cleanupList.emplace_back(::App::Config::Instance().Runtime.ShowControlWindow.OnChangeListener([this](::App::Config::ItemBase&) {
		ShowWindow(m_hWnd, ::App::Config::Instance().Runtime.ShowControlWindow ? SW_SHOW : SW_HIDE);
		}));
	if (::App::Config::Instance().Runtime.ShowControlWindow)
		ShowWindow(m_hWnd, SW_SHOW);

	Network::SocketHook::Instance()->AddOnSocketFoundListener(this, [this](Network::SingleConnection&) {
		InvalidateRect(m_hWnd, nullptr, false);
		});
	Network::SocketHook::Instance()->AddOnSocketGoneListener(this, [this](Network::SingleConnection&) {
		InvalidateRect(m_hWnd, nullptr, false);
		});
	m_cleanupList.emplace_back([this]() { Network::SocketHook::Instance()->RemoveListeners(this); });
}

App::Window::Main::~Main() {
	while (!m_cleanupList.empty())
		m_cleanupList.pop_back();
	Destroy();
}

LRESULT App::Window::Main::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (!lParam && MessageBoxW(m_hWnd, L"Closing this window will unload XivAlexander. Disable \"Show Control Window\" from the menu to hide this window. Proceed?", L"XivAlexander", MB_YESNO | MB_ICONQUESTION) == IDNO) {
			return 0;
		}
	} else if (uMsg == WM_INITMENUPOPUP) {
		RepopulateMenu(GetMenu(m_hWnd));
	} else if (uMsg == WM_COMMAND) {
		if (!lParam) {
			auto& config = ::App::Config::Instance().Runtime;
			switch (LOWORD(wParam)) {
				
				case ID_TRAYMENU_KEEPGAMEWINDOWALWAYSONTOP:
					config.AlwaysOnTop = !config.AlwaysOnTop;
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_ENABLE:
					config.UseHighLatencyMitigation = !config.UseHighLatencyMitigation;
					return 0;

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_USEDELAYDETECTION:
					config.UseAutoAdjustingExtraDelay = !config.UseAutoAdjustingExtraDelay;
					return 0;

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_USELATENCYCORRECTION:
					config.UseLatencyCorrection = !config.UseLatencyCorrection;
					return 0;

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_USELOGGING:
					config.UseHighLatencyMitigationLogging = !config.UseHighLatencyMitigationLogging;
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_NETWORKING_REDUCEPACKETDELAY:
					config.ReducePacketDelay = !config.ReducePacketDelay;
					return 0;

				case ID_TRAYMENU_NETWORKING_TAKEOVERLOOPBACKADDRESSES:
					config.TakeOverLoopbackAddresses = !config.TakeOverLoopbackAddresses;
					return 0;

				case ID_TRAYMENU_NETWORKING_TAKEOVERPRIVATEADDRESSES:
					config.TakeOverPrivateAddresses = !config.TakeOverPrivateAddresses;
					return 0;

				case ID_TRAYMENU_NETWORKING_TAKEOVERALLADDRESSES:
					config.TakeOverAllAddresses = !config.TakeOverAllAddresses;
					return 0;

				case ID_TRAYMENU_NETWORKING_TAKEOVERALLPORTS:
					config.TakeOverAllPorts = !config.TakeOverAllPorts;
					return 0;

				case ID_TRAYMENU_NETWORKING_RELEASEALLCONNECTIONS:
					App::Instance()->RunOnGameLoop([]() {
						Network::SocketHook::Instance()->ReleaseSockets();
					});
					return 0;

					/***************************************************************/
					
				case ID_TRAYMENU_USEALLIPCMESSAGELOGGER:
					config.UseAllIpcMessageLogger = !config.UseAllIpcMessageLogger;
					return 0;

				case ID_TRAYMENU_USEIPCTYPEFINDER:
					config.UseOpcodeFinder = !config.UseOpcodeFinder;
					return 0;

				case ID_TRAYMENU_USEEFFECTAPPLICATIONDELAYLOGGER:
					config.UseEffectApplicationDelayLogger = !config.UseEffectApplicationDelayLogger;
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

				case ID_TRAYMENU_EDITRUNTIMECONFIGURATION:
					if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
						SetFocus(m_runtimeConfigEditor->GetHandle());
					else
						m_runtimeConfigEditor = std::make_unique<Config>(&::App::Config::Instance().Runtime);
					return 0;

				case ID_TRAYMENU_EDITOPCODECONFIGURATION:
					if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
						SetFocus(m_gameConfigEditor->GetHandle());
					else
						m_gameConfigEditor = std::make_unique<Config>(&::App::Config::Instance().Game);
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_CHECKFORUPDATES:
					App::Instance()->CheckUpdates(false);
					return 0;

				case ID_TRAYMENU_UNLOADXIVALEXANDER:
					m_triggerUnload();
					return 0;

					/***************************************************************/

				case ID_VIEW_ALWAYSONTOP:
				{
					if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) {
						SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					} else {
						SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					}
					Utils::Win32::SetMenuState(m_hWnd, ID_VIEW_ALWAYSONTOP, GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE)& WS_EX_TOPMOST);
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
				nullptr
			);

			if (result)
				SendMessage(m_hWnd, WM_COMMAND, MAKEWPARAM(result, 0), 0);
		}
	} else if (uMsg == m_uTaskbarRestartMessage) {
		RegisterTrayIcon();
	} else if (uMsg == WM_TIMER) {
		if (wParam == TimerIdReregisterTrayIcon) {
			RegisterTrayIcon();
		} else if (wParam == TimerIdRepaint) {
			InvalidateRect(m_hWnd, nullptr, false);
		}
	} else if (uMsg == WM_PAINT) {
		PAINTSTRUCT ps{};
		RECT rect{};
		const auto hdc = BeginPaint(m_hWnd, &ps);
		
		const auto zoom = GetZoom();
		NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
		SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
		
		GetClientRect(m_hWnd, &rect);
		const auto backdc = CreateCompatibleDC(hdc);
		
		std::vector<HGDIOBJ> gdiRestoreStack;
		gdiRestoreStack.emplace_back(SelectObject(backdc, CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top)));
		gdiRestoreStack.emplace_back(SelectObject(backdc, CreateFontIndirectW(&ncm.lfMessageFont)));
		
		FillRect(backdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		const auto str = std::format(
			L"Process ID: {}\n"
			L"Game Path: {}\n"
			L"Game Release: {} ({})\n"
			L"\n"
			L"{}\n"
			L"Tips:\n"
			L"* Turn off \"Use Delay Detection\" and \"Use Latency Correction\" if any of the following is true.\n"
			L"  * You're using a VPN software and your latency is being displayed below 10ms when it shouldn't be.\n"
			L"  * Your ping is above 200ms.\n"
			L"  * You can't double weave comfortably.",
			GetCurrentProcessId(), m_path, m_sVersion, m_sRegion,
			Network::SocketHook::Instance()->Describe());
		const auto pad = static_cast<int>(8 * zoom);
		RECT rct = {
			pad,
			pad,
			rect.right - pad,
			rect.bottom - pad,
		};
		DrawTextW(backdc, &str[0], -1, &rct, DT_TOP | DT_LEFT | DT_NOCLIP | DT_EDITCONTROL | DT_WORDBREAK);

		BitBlt(hdc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, backdc, 0, 0, SRCCOPY);

		while (!gdiRestoreStack.empty()) {
			DeleteObject(SelectObject(backdc, gdiRestoreStack.back()));
			gdiRestoreStack.pop_back();
		}
		DeleteDC(backdc);

		EndPaint(m_hWnd, &ps);
		return 0;
	}
	return BaseWindow::WndProc(uMsg, wParam, lParam);
}

void App::Window::Main::OnDestroy() {
	m_triggerUnload();

	m_runtimeConfigEditor = nullptr;
	m_gameConfigEditor = nullptr;
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.guidItem = m_guid;
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	nid.uFlags = NIF_GUID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
	BaseWindow::OnDestroy();
	PostQuitMessage(0);
}

void App::Window::Main::RepopulateMenu(HMENU hMenu) {
	const auto& config = ::App::Config::Instance().Runtime;

	Utils::Win32::SetMenuState(hMenu, ID_VIEW_ALWAYSONTOP, GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
	
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_KEEPGAMEWINDOWALWAYSONTOP, config.AlwaysOnTop);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_ENABLE, config.UseHighLatencyMitigation);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USEDELAYDETECTION, config.UseAutoAdjustingExtraDelay);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USELATENCYCORRECTION, config.UseLatencyCorrection);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USELOGGING, config.UseHighLatencyMitigationLogging);

	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_NETWORKING_REDUCEPACKETDELAY, config.ReducePacketDelay);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERLOOPBACKADDRESSES, config.TakeOverLoopbackAddresses);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERPRIVATEADDRESSES, config.TakeOverPrivateAddresses);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERALLADDRESSES, config.TakeOverAllAddresses);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERALLPORTS, config.TakeOverAllPorts);
	
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, config.UseOpcodeFinder);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_USEALLIPCMESSAGELOGGER, config.UseAllIpcMessageLogger);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_USEEFFECTAPPLICATIONDELAYLOGGER, config.UseEffectApplicationDelayLogger);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_SHOWCONTROLWINDOW, config.ShowControlWindow);
	Utils::Win32::SetMenuState(hMenu, ID_TRAYMENU_SHOWLOGGINGWINDOW, config.ShowLoggingWindow);
}

void App::Window::Main::RegisterTrayIcon() {
	const auto hIcon = Utils::Win32::Closeable::Icon(LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"Failed to load app icon.");
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.uVersion = NOTIFYICON_VERSION_4;
	nid.guidItem = m_guid;
	nid.uID = TrayItemId;
	nid.hWnd = this->m_hWnd;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
	nid.uCallbackMessage = WmTrayCallback;
	nid.hIcon = hIcon;
	wcscpy_s(nid.szTip, std::format(L"XivAlexander({})", GetCurrentProcessId()).c_str());
	Shell_NotifyIconW(NIM_ADD, &nid);
	Shell_NotifyIconW(NIM_SETVERSION, &nid);
}
