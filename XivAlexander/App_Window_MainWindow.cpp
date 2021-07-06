#include "pch.h"
#include "resource.h"
#include "App_XivAlexApp.h"
#include "App_Window_ConfigWindow.h"
#include "App_Window_MainWindow.h"
#include "App_Network_SocketHook.h"
#include "XivAlexander/XivAlexander.h"

static const auto WmTrayCallback = WM_APP + 1;
static const int TrayItemId = 1;
static const int TimerIdReregisterTrayIcon = 100;
static const int TimerIdRepaint = 101;

static WNDCLASSEXW WindowClass() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"Failed to load app icon.");
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = Dll::Module();
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_TRAY_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::Main";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::Main::Main(XivAlexApp* pApp, std::function<void()> unloadFunction)
	: BaseWindow(WindowClass(), L"XivAlexander", WS_OVERLAPPEDWINDOW, WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 480, 160, nullptr, nullptr)
	, m_pApp(pApp)
	, m_triggerUnload(std::move(unloadFunction))
	, m_uTaskbarRestartMessage(RegisterWindowMessageW(L"TaskbarCreated"))
	, m_path(Utils::Win32::Process::Current().PathOf())
	, m_bUseElevation(Utils::Win32::IsUserAnAdmin())
	, m_launchParameters([this]() -> decltype(m_launchParameters) {
		try {
			return XivAlex::ParseGameCommandLine(Utils::ToUtf8(Utils::Win32::GetCommandLineWithoutProgramName()), &m_bUseParameterObfuscation);
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::General, "Could not resolve game launch parameters. Restart feature will be disabled. ({})", e.what());
			return {};
		}
	}()) {

	std::tie(m_sRegion, m_sVersion) = XivAlex::ResolveGameReleaseRegion();

	const auto title = std::format(L"XivAlexander: {}, {}, {}", GetCurrentProcessId(), m_sRegion, m_sVersion);
	SetWindowTextW(m_hWnd, title.c_str());
	ModifyMenu(GetMenu(m_hWnd), ID_TRAYMENU_CURRENTINFO, MF_BYCOMMAND | MF_DISABLED, ID_TRAYMENU_CURRENTINFO, title.c_str());

	RegisterTrayIcon();

	// Try to restore tray icon every 5 seconds in case things go wrong
	SetTimer(m_hWnd, TimerIdReregisterTrayIcon, 5000, nullptr);

	SetTimer(m_hWnd, TimerIdRepaint, 1000, nullptr);

	m_cleanup += m_config->Runtime.ShowControlWindow.OnChangeListener([this](auto&) {
		ShowWindow(m_hWnd, m_config->Runtime.ShowControlWindow ? SW_SHOW : SW_HIDE);
		});
	if (m_config->Runtime.ShowControlWindow)
		ShowWindow(m_hWnd, SW_SHOW);

	m_cleanup += m_pApp->GetSocketHook()->OnSocketFound([this](auto&) {
		InvalidateRect(m_hWnd, nullptr, false);
		});
	m_cleanup += m_pApp->GetSocketHook()->OnSocketGone([this](auto&) {
		InvalidateRect(m_hWnd, nullptr, false);
		});
}

App::Window::Main::~Main() {
	m_cleanup.Clear();
	Destroy();
}

LRESULT App::Window::Main::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (lParam)
			DestroyWindow(m_hWnd);
		else {
			switch (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNOCANCEL | MB_ICONQUESTION, L"XivAlexander",
				L"Do you want to unload XivAlexander?\n\n"
				L"Press Yes to unload XivAlexander.\n"
				L"Press No to hide this window.\n"
				L"Press Cancel to do nothing.")) {
				case IDYES:
					m_triggerUnload();
					break;
				case IDNO:
					m_config->Runtime.ShowControlWindow = false;
					break;
			}
		}
		return 0;
	} else if (uMsg == WM_INITMENUPOPUP) {
		RepopulateMenu(GetMenu(m_hWnd));
	} else if (uMsg == WM_COMMAND) {
		if (!lParam) {
			auto& config = m_config->Runtime;
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
					m_pApp->RunOnGameLoop([this]() {
						m_pApp->GetSocketHook()->ReleaseSockets();
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
					SetForegroundWindow(m_hWnd);
					return 0;

				case ID_TRAYMENU_EDITRUNTIMECONFIGURATION:
					if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
						SetForegroundWindow(m_runtimeConfigEditor->GetHandle());
					else
						m_runtimeConfigEditor = std::make_unique<Config>(&m_config->Runtime);
					return 0;

				case ID_TRAYMENU_EDITOPCODECONFIGURATION:
					if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
						SetForegroundWindow(m_gameConfigEditor->GetHandle());
					else
						m_gameConfigEditor = std::make_unique<Config>(&m_config->Game);
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_RESTARTGAME_RESTART:
					AskRestartGame();
					return 0;
				
				case ID_TRAYMENU_RESTARTGAME_USEDIRECTX11: 
					m_bUseDirectX11 = !m_bUseDirectX11;
					if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))
						AskRestartGame();
					return 0;
				
				case ID_TRAYMENU_RESTARTGAME_USEXIVALEXANDER:
					m_bUseXivAlexander = !m_bUseXivAlexander;
					if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))
						AskRestartGame();
					return 0;
				
				case ID_TRAYMENU_RESTARTGAME_USEPARAMETEROBFUSCATION:
					m_bUseParameterObfuscation = !m_bUseParameterObfuscation;
					if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))
						AskRestartGame();
					return 0;
				
				case ID_TRAYMENU_RESTARTGAME_USEELEVATION:
					m_bUseElevation = !m_bUseElevation;
					if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))
						AskRestartGame();
					return 0;
					
				case ID_TRAYMENU_EXITGAME:
					if (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, L"XivAlexander", L"Exit game?") == IDYES) {
						RemoveTrayIcon();
						ExitProcess(0);
					}
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_CHECKFORUPDATES:
					XivAlexDll::LaunchXivAlexLoaderWithTargetHandles({Utils::Win32::Process::Current()}, XivAlexDll::LoaderAction::UpdateCheck, false);
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
					Utils::Win32::SetMenuState(GetMenu(m_hWnd), ID_VIEW_ALWAYSONTOP, GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST, true);
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

			BOOL result;

			{
				const auto temporaryFocus = WithTemporaryFocus();
				result = TrackPopupMenu(
					GetSubMenu(GetMenu(m_hWnd), 0),
					TPM_RETURNCMD | TPM_NONOTIFY,
					curPoint.x,
					curPoint.y,
					0,
					m_hWnd,
					nullptr
				);
			}

			if (result)
				SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(result, 0), 0);
		} else if (eventId == WM_LBUTTONUP) {
			const auto now = GetTickCount64();
			auto willShowControlWindow = false;
			if (m_lastTrayIconLeftButtonUp + GetDoubleClickTime() > now) {
				if ((m_config->Runtime.ShowControlWindow = !m_config->Runtime.ShowControlWindow))
					willShowControlWindow = true;
				m_lastTrayIconLeftButtonUp = 0;
			} else
				m_lastTrayIconLeftButtonUp = now;

			if (!willShowControlWindow) {
				const auto temporaryFocus = WithTemporaryFocus();
				SetForegroundWindow(m_pApp->GetGameWindowHandle());
			} else {
				SetForegroundWindow(m_hWnd);
			}
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
			m_pApp->GetSocketHook()->Describe());
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
	RemoveTrayIcon();
	BaseWindow::OnDestroy();
	PostQuitMessage(0);
}

void App::Window::Main::RepopulateMenu(HMENU hMenu) {
	const auto& config = m_config->Runtime;
	const auto Set = Utils::Win32::SetMenuState;

	Set(hMenu, ID_VIEW_ALWAYSONTOP, GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST, true);
	
	Set(hMenu, ID_TRAYMENU_KEEPGAMEWINDOWALWAYSONTOP, config.AlwaysOnTop, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_ENABLE, config.UseHighLatencyMitigation, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USEDELAYDETECTION, config.UseAutoAdjustingExtraDelay, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USELATENCYCORRECTION, config.UseLatencyCorrection, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USELOGGING, config.UseHighLatencyMitigationLogging, true);
	
	Set(hMenu, ID_TRAYMENU_NETWORKING_REDUCEPACKETDELAY, config.ReducePacketDelay, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERLOOPBACKADDRESSES, config.TakeOverLoopbackAddresses, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERPRIVATEADDRESSES, config.TakeOverPrivateAddresses, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERALLADDRESSES, config.TakeOverAllAddresses, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERALLPORTS, config.TakeOverAllPorts, true);
	
	Set(hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, config.UseOpcodeFinder, true);
	Set(hMenu, ID_TRAYMENU_USEALLIPCMESSAGELOGGER, config.UseAllIpcMessageLogger, true);
	Set(hMenu, ID_TRAYMENU_USEEFFECTAPPLICATIONDELAYLOGGER, config.UseEffectApplicationDelayLogger, true);
	Set(hMenu, ID_TRAYMENU_SHOWCONTROLWINDOW, config.ShowControlWindow, true);
	Set(hMenu, ID_TRAYMENU_SHOWLOGGINGWINDOW, config.ShowLoggingWindow, true);
	
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_RESTART, false, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEDIRECTX11, m_bUseDirectX11, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEXIVALEXANDER, m_bUseXivAlexander, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEPARAMETEROBFUSCATION, m_bUseParameterObfuscation, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEELEVATION, m_bUseElevation, !m_launchParameters.empty());
}

void App::Window::Main::RegisterTrayIcon() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"Failed to load app icon.");
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.uVersion = NOTIFYICON_VERSION_4;
	nid.uID = TrayItemId;
	nid.hWnd = this->m_hWnd;
	nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
	nid.uCallbackMessage = WmTrayCallback;
	nid.hIcon = hIcon;
	wcscpy_s(nid.szTip, std::format(L"XivAlexander({})", GetCurrentProcessId()).c_str());
	Shell_NotifyIconW(NIM_ADD, &nid);
	Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void App::Window::Main::RemoveTrayIcon() {
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	nid.uFlags = NIF_GUID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

void App::Window::Main::AskRestartGame() {
	if (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, L"XivAlexander", 
		L"Restart game?\n\n"
		L"Use DirectX 11: {}\n"
		L"Use XivAlexander: {}\n"
		L"Use Parameter Obfuscation: {}\n"
		L"Use Elevation (Run as Administrator): {}\n",
		m_bUseDirectX11 ? "Yes" : "No",
		m_bUseXivAlexander ? "Yes" : "No",
		m_bUseParameterObfuscation ? "Yes" : "No",
		m_bUseElevation ? "Yes" : "No"
	) == IDYES) {
		const auto process = Utils::Win32::Process::Current();
		try {
			const auto game = process.PathOf().parent_path() / (m_bUseDirectX11 ? XivAlex::GameExecutable64NameW : XivAlex::GameExecutable32NameW);
			
			XivAlexDll::EnableInjectOnCreateProcess(0);
			bool ok;
			if (m_bUseXivAlexander)
				ok = Utils::Win32::RunProgram({
					.path = Dll::Module().PathOf().parent_path() / (m_bUseDirectX11 ? XivAlex::XivAlexLoader64NameW : XivAlex::XivAlexLoader32NameW),
					.args = std::format(L"-a launcher -l select \"{}\" {}", game, XivAlex::CreateGameCommandLine(m_launchParameters, m_bUseParameterObfuscation)),
					.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
				});
			else
				ok = Utils::Win32::RunProgram({
					.path = game,
					.args = Utils::FromUtf8(XivAlex::CreateGameCommandLine(m_launchParameters, m_bUseParameterObfuscation)),
					.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
				});

			if (ok) {
				RemoveTrayIcon();
				process.Terminate(0);
			}
			
		} catch (const std::exception& e) {
			Utils::Win32::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, L"XivAlexander", L"Failed to restart: {}", e.what());
		}
		XivAlexDll::EnableInjectOnCreateProcess(XivAlexDll::InjectOnCreateProcessAppFlags::Use | XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly);
	}
}
