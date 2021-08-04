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
		"LoadIconW");
	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = Dll::Module();
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
	wcex.lpszClassName = L"XivAlexander::Window::Main";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::Main::Main(XivAlexApp* pApp, std::function<void()> unloadFunction)
	: BaseWindow(WindowClass(), nullptr, WS_OVERLAPPEDWINDOW, WS_EX_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, 480, 160, nullptr, nullptr)
	, m_pApp(pApp)
	, m_triggerUnload(std::move(unloadFunction))
	, m_uTaskbarRestartMessage(RegisterWindowMessageW(L"TaskbarCreated"))
	, m_path(Utils::Win32::Process::Current().PathOf())
	, m_bUseElevation(Utils::Win32::IsUserAnAdmin())
	, m_launchParameters([this]() -> decltype(m_launchParameters) {
	try {
		return XivAlex::ParseGameCommandLine(Utils::ToUtf8(Utils::Win32::GetCommandLineWithoutProgramName()), &m_bUseParameterObfuscation);
	} catch (const std::exception& e) {
		m_logger->Format<LogLevel::Warning>(LogCategory::General, m_config->Runtime.GetLangId(), IDS_WARNING_GAME_PARAMETER_PARSE, e.what());
		return {};
	}
}()) {

	std::tie(m_sRegion, m_sVersion) = XivAlex::ResolveGameReleaseRegion();

	if (m_sRegion == L"JP" && !m_launchParameters.empty()) {
		m_gameLanguage = App::Config::GameLanguage::English;
		m_gameRegion = App::Config::GameRegion::Japan;
		for (const auto& pair : m_launchParameters) {
			if (pair.first == "language")
				m_gameLanguage = static_cast<App::Config::GameLanguage>(1 + std::strtol(pair.second.c_str(), nullptr, 0));
			else if (pair.first == "SYS.Region")
				m_gameRegion = static_cast<App::Config::GameRegion>(std::strtol(pair.second.c_str(), nullptr, 0));
		}
	} else if (m_sRegion == L"CN") {
		m_gameLanguage = App::Config::GameLanguage::ChineseSimplified;
		m_gameRegion = App::Config::GameRegion::China;
	} else if (m_sRegion == L"KR") {
		m_gameLanguage = App::Config::GameLanguage::Korean;
		m_gameRegion = App::Config::GameRegion::Korea;
	}

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
	ApplyLanguage(m_config->Runtime.GetLangId());
}

App::Window::Main::~Main() {
	m_cleanup.Clear();
	Destroy();
}

void App::Window::Main::ShowContextMenu(const BaseWindow* parent) const {
	if (!parent)
		parent = this;

	const auto hMenu = GetMenu(m_hWnd);
	const auto hSubMenu = GetSubMenu(hMenu, 0);
	RepopulateMenu(hSubMenu);
	POINT curPoint;
	GetCursorPos(&curPoint);

	BOOL result;

	{
		const auto temporaryFocus = parent->WithTemporaryFocus();
		result = TrackPopupMenu(
			GetSubMenu(GetMenu(m_hWnd), 0),
			TPM_RETURNCMD | TPM_NONOTIFY,
			curPoint.x,
			curPoint.y,
			0,
			parent->GetHandle(),
			nullptr
		);
	}

	if (result)
		SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(result, 0), 0);
}

void App::Window::Main::ApplyLanguage(WORD languageId) {
	m_hAcceleratorWindow = { Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_TRAY_ACCELERATOR), languageId };
	m_hAcceleratorThread = { Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_TRAY_GLOBAL_ACCELERATOR), languageId };
	Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_TRAY_MENU), m_config->Runtime.GetLangId()).AttachAndSwap(m_hWnd);

	const auto title = std::format(L"{}: {}, {}, {}",
		m_config->Runtime.GetStringRes(IDS_APP_NAME), GetCurrentProcessId(), m_sRegion, m_sVersion);
	SetWindowTextW(m_hWnd, title.c_str());
	ModifyMenuW(GetMenu(m_hWnd), ID_TRAYMENU_CURRENTINFO, MF_BYCOMMAND | MF_DISABLED, ID_TRAYMENU_CURRENTINFO, title.c_str());
	InvalidateRect(m_hWnd, nullptr, FALSE);
}

LRESULT App::Window::Main::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (lParam)
			DestroyWindow(m_hWnd);
		else {
			switch (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNOCANCEL | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_APP_NAME),
				m_config->Runtime.FormatStringRes(IDS_CONFIRM_MAIN_WINDOW_CLOSE,
					Utils::Win32::MB_GetString(IDYES - 1),
					Utils::Win32::MB_GetString(IDNO - 1),
					Utils::Win32::MB_GetString(IDCANCEL - 1)
				))) {
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

				case ID_GLOBAL_SHOW_TRAYMENU:
					for (const auto& w : BaseWindow::All())
						if (w->GetHandle() == GetForegroundWindow())
							ShowContextMenu(w);
					return 0;

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

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_USEEARLYPENALTY:
					config.UseEarlyPenalty = !config.UseEarlyPenalty;
					return 0;

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_USELOGGING:
					config.UseHighLatencyMitigationLogging = !config.UseHighLatencyMitigationLogging;
					return 0;

				case ID_TRAYMENU_HIGHLATENCYMITIGATION_PREVIEWMODE:
					config.UseHighLatencyMitigationPreviewMode = !config.UseHighLatencyMitigationPreviewMode;
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

				case ID_TRAYMENU_HASHKEYMANIPULATION_ENABLE:
					config.UseHashTracker = !config.UseHashTracker;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LOGALLHASHKEYS:
					config.UseHashTrackerKeyLogging = !config.UseHashTrackerKeyLogging;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_NONE:
					config.HashTrackerLanguageOverride = App::Config::GameLanguage::Unspecified;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_ENGLISH:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::English))
						config.HashTrackerLanguageOverride = App::Config::GameLanguage::English;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_GERMAN:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::German))
						config.HashTrackerLanguageOverride = App::Config::GameLanguage::German;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_FRENCH:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::French))
						config.HashTrackerLanguageOverride = App::Config::GameLanguage::French;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_JAPANESE:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::Japanese))
						config.HashTrackerLanguageOverride = App::Config::GameLanguage::Japanese;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_SIMPLIFIEDCHINESE:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::ChineseSimplified))
						config.HashTrackerLanguageOverride = App::Config::GameLanguage::ChineseSimplified;
					return 0;

				case ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_KOREAN:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::Korean))
						config.HashTrackerLanguageOverride = App::Config::GameLanguage::Korean;
					return 0;

				case ID_TRAYMENU_CONFIGURATION_SHOWLOGGINGWINDOW:
					config.ShowLoggingWindow = !config.ShowLoggingWindow;
					return 0;

				case ID_TRAYMENU_CONFIGURATION_SHOWCONTROLWINDOW:
					config.ShowControlWindow = !config.ShowControlWindow;
					SetForegroundWindow(m_hWnd);
					return 0;

				case ID_TRAYMENU_CONFIGURATION_EDITRUNTIMECONFIGURATION:
					if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
						SetForegroundWindow(m_runtimeConfigEditor->GetHandle());
					else
						m_runtimeConfigEditor = std::make_unique<Config>(IDS_WINDOW_RUNTIME_CONFIG_EDITOR, &m_config->Runtime);
					return 0;

				case ID_TRAYMENU_CONFIGURATION_EDITOPCODECONFIGURATION:
					if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
						SetForegroundWindow(m_gameConfigEditor->GetHandle());
					else
						m_gameConfigEditor = std::make_unique<Config>(IDS_WINDOW_OPCODE_CONFIG_EDITOR, &m_config->Game);
					return 0;

				case ID_TRAYMENU_CONFIGURATION_LANGUAGE_SYSTEMDEFAULT:
					config.Language = App::Config::Language::SystemDefault;
					return 0;

				case ID_TRAYMENU_CONFIGURATION_LANGUAGE_ENGLISH:
					config.Language = App::Config::Language::English;
					return 0;

				case ID_TRAYMENU_CONFIGURATION_LANGUAGE_KOREAN:
					config.Language = App::Config::Language::Korean;
					return 0;

				case ID_TRAYMENU_CONFIGURATION_LANGUAGE_JAPANESE:
					config.Language = App::Config::Language::Japanese;
					return 0;

				case ID_TRAYMENU_CONFIGURATION_RELOAD:
					config.Reload();
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_RESTARTGAME_RESTART:
					AskRestartGame();
					return 0;

				case ID_TRAYMENU_RESTARTGAME_USEDIRECTX11:
					m_bUseDirectX11 = !m_bUseDirectX11;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_RESTARTGAME_USEXIVALEXANDER:
					m_bUseXivAlexander = !m_bUseXivAlexander;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_RESTARTGAME_USEPARAMETEROBFUSCATION:
					m_bUseParameterObfuscation = !m_bUseParameterObfuscation;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_RESTARTGAME_USEELEVATION:
					m_bUseElevation = !m_bUseElevation;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_RESTARTGAME_LANGUAGE_ENGLISH:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::English)) {
						m_gameLanguage = App::Config::GameLanguage::English;
						AskRestartGame(true);
					}
					return 0;

				case ID_TRAYMENU_RESTARTGAME_LANGUAGE_GERMAN:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::German)) {
						m_gameLanguage = App::Config::GameLanguage::German;
						AskRestartGame(true);
					}
					return 0;

				case ID_TRAYMENU_RESTARTGAME_LANGUAGE_FRENCH:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::French)) {
						m_gameLanguage = App::Config::GameLanguage::French;
						AskRestartGame(true);
					}
					return 0;

				case ID_TRAYMENU_RESTARTGAME_LANGUAGE_JAPANESE:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::Japanese)) {
						m_gameLanguage = App::Config::GameLanguage::Japanese;
						AskRestartGame(true);
					}
					return 0;

				case ID_TRAYMENU_RESTARTGAME_LANGUAGE_SIMPLIFIEDCHINESE:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::ChineseSimplified)) {
						m_gameLanguage = App::Config::GameLanguage::ChineseSimplified;
						AskRestartGame(true);
					}
					return 0;

				case ID_TRAYMENU_RESTARTGAME_LANGUAGE_KOREAN:
					if (AskUpdateGameLanguageOverride(App::Config::GameLanguage::Korean)) {
						m_gameLanguage = App::Config::GameLanguage::Korean;
						AskRestartGame(true);
					}
					return 0;

				case ID_TRAYMENU_RESTARTGAME_REGION_JAPAN:
					m_gameRegion = App::Config::GameRegion::Japan;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_RESTARTGAME_REGION_NORTH_AMERICA:
					m_gameRegion = App::Config::GameRegion::NorthAmerica;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_RESTARTGAME_REGION_EUROPE:
					m_gameRegion = App::Config::GameRegion::Europe;
					AskRestartGame(true);
					return 0;

				case ID_TRAYMENU_EXITGAME:
					if (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_APP_NAME),
						m_config->Runtime.GetStringRes(IDS_CONFIRM_EXIT_GAME)) == IDYES) {
						RemoveTrayIcon();
						ExitProcess(0);
					}
					return 0;

					/***************************************************************/

				case ID_TRAYMENU_CHECKFORUPDATES:
					try {
						LaunchXivAlexLoaderWithTargetHandles({ Utils::Win32::Process::Current() }, XivAlexDll::LoaderAction::UpdateCheck, false);
					} catch (const std::exception& e) {
						Utils::Win32::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.GetStringRes(IDS_APP_NAME),
							m_config->Runtime.FormatStringRes(IDS_ERROR_UPDATE_CHECK_LAUNCH, e.what()));
					}
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
			ShowContextMenu();
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
		const auto str = m_config->Runtime.FormatStringRes(IDS_FAQ,
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
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
}

void App::Window::Main::OnDestroy() {
	m_triggerUnload();

	m_runtimeConfigEditor = nullptr;
	m_gameConfigEditor = nullptr;
	RemoveTrayIcon();
	BaseWindow::OnDestroy();
	PostQuitMessage(0);
}

void App::Window::Main::RepopulateMenu(HMENU hMenu) const {
	const auto& config = m_config->Runtime;
	const auto Set = Utils::Win32::SetMenuState;

	Set(hMenu, ID_VIEW_ALWAYSONTOP, GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST, true);

	Set(hMenu, ID_TRAYMENU_KEEPGAMEWINDOWALWAYSONTOP, config.AlwaysOnTop, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_ENABLE, config.UseHighLatencyMitigation, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USEDELAYDETECTION, config.UseAutoAdjustingExtraDelay, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USELATENCYCORRECTION, config.UseLatencyCorrection, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USEEARLYPENALTY, config.UseEarlyPenalty, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_USELOGGING, config.UseHighLatencyMitigationLogging, true);
	Set(hMenu, ID_TRAYMENU_HIGHLATENCYMITIGATION_PREVIEWMODE, config.UseHighLatencyMitigationPreviewMode, true);

	Set(hMenu, ID_TRAYMENU_NETWORKING_REDUCEPACKETDELAY, config.ReducePacketDelay, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERLOOPBACKADDRESSES, config.TakeOverLoopbackAddresses, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERPRIVATEADDRESSES, config.TakeOverPrivateAddresses, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERALLADDRESSES, config.TakeOverAllAddresses, true);
	Set(hMenu, ID_TRAYMENU_NETWORKING_TAKEOVERALLPORTS, config.TakeOverAllPorts, true);

	Set(hMenu, ID_TRAYMENU_USEIPCTYPEFINDER, config.UseOpcodeFinder, true);
	Set(hMenu, ID_TRAYMENU_USEALLIPCMESSAGELOGGER, config.UseAllIpcMessageLogger, true);
	Set(hMenu, ID_TRAYMENU_USEEFFECTAPPLICATIONDELAYLOGGER, config.UseEffectApplicationDelayLogger, true);

	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_ENABLE, config.UseHashTracker, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LOGALLHASHKEYS, config.UseHashTrackerKeyLogging, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_NONE, config.HashTrackerLanguageOverride == App::Config::GameLanguage::Unspecified, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_ENGLISH, config.HashTrackerLanguageOverride == App::Config::GameLanguage::English, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_GERMAN, config.HashTrackerLanguageOverride == App::Config::GameLanguage::German, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_FRENCH, config.HashTrackerLanguageOverride == App::Config::GameLanguage::French, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_JAPANESE, config.HashTrackerLanguageOverride == App::Config::GameLanguage::Japanese, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_SIMPLIFIEDCHINESE, config.HashTrackerLanguageOverride == App::Config::GameLanguage::ChineseSimplified, true);
	Set(hMenu, ID_TRAYMENU_HASHKEYMANIPULATION_LANGUAGE_KOREAN, config.HashTrackerLanguageOverride == App::Config::GameLanguage::Korean, true);

	Set(hMenu, ID_TRAYMENU_CONFIGURATION_SHOWCONTROLWINDOW, config.ShowControlWindow, true);
	Set(hMenu, ID_TRAYMENU_CONFIGURATION_SHOWLOGGINGWINDOW, config.ShowLoggingWindow, true);
	Set(hMenu, ID_TRAYMENU_CONFIGURATION_LANGUAGE_SYSTEMDEFAULT, config.Language == App::Config::Language::SystemDefault, true);
	Set(hMenu, ID_TRAYMENU_CONFIGURATION_LANGUAGE_ENGLISH, config.Language == App::Config::Language::English, true);
	Set(hMenu, ID_TRAYMENU_CONFIGURATION_LANGUAGE_KOREAN, config.Language == App::Config::Language::Korean, true);
	Set(hMenu, ID_TRAYMENU_CONFIGURATION_LANGUAGE_JAPANESE, config.Language == App::Config::Language::Japanese, true);

	Set(hMenu, ID_TRAYMENU_RESTARTGAME_RESTART, false, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEDIRECTX11, m_bUseDirectX11, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEXIVALEXANDER, m_bUseXivAlexander, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEPARAMETEROBFUSCATION, m_bUseParameterObfuscation, !m_launchParameters.empty());
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_USEELEVATION, m_bUseElevation, !m_launchParameters.empty());
	const auto languageRegionModifiable = LanguageRegionModifiable();
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_LANGUAGE_ENGLISH, m_gameLanguage == App::Config::GameLanguage::English, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_LANGUAGE_GERMAN, m_gameLanguage == App::Config::GameLanguage::German, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_LANGUAGE_FRENCH, m_gameLanguage == App::Config::GameLanguage::French, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_LANGUAGE_JAPANESE, m_gameLanguage == App::Config::GameLanguage::Japanese, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_LANGUAGE_SIMPLIFIEDCHINESE, m_gameLanguage == App::Config::GameLanguage::ChineseSimplified, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_LANGUAGE_KOREAN, m_gameLanguage == App::Config::GameLanguage::Korean, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_REGION_JAPAN, m_gameRegion == App::Config::GameRegion::Japan, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_REGION_NORTH_AMERICA, m_gameRegion == App::Config::GameRegion::NorthAmerica, languageRegionModifiable);
	Set(hMenu, ID_TRAYMENU_RESTARTGAME_REGION_EUROPE, m_gameRegion == App::Config::GameRegion::Europe, languageRegionModifiable);
}

void App::Window::Main::RegisterTrayIcon() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"LoadIconW");
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

bool App::Window::Main::LanguageRegionModifiable() const {
	return m_gameRegion == App::Config::GameRegion::Japan
		|| m_gameRegion == App::Config::GameRegion::NorthAmerica
		|| m_gameRegion == App::Config::GameRegion::Europe;
}

void App::Window::Main::AskRestartGame(bool onlyOnModifer) {
	if (onlyOnModifer && !((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))) {
		return;
	}
	const auto yes = Utils::Win32::MB_GetString(IDYES - 1);
	const auto no = Utils::Win32::MB_GetString(IDNO - 1);
	if (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_APP_NAME), m_config->Runtime.FormatStringRes(
		IDS_CONFIRM_RESTART_GAME,
		m_bUseDirectX11 ? yes : no,
		m_bUseXivAlexander ? yes : no,
		m_bUseParameterObfuscation ? yes : no,
		m_bUseElevation ? yes : no,
		m_config->Runtime.GetLanguageNameLocalized(m_gameLanguage),
		m_config->Runtime.GetRegionNameLocalized(m_gameRegion)
	)) == IDYES) {
		const auto process = Utils::Win32::Process::Current();
		try {
			const auto game = process.PathOf().parent_path() / (m_bUseDirectX11 ? XivAlex::GameExecutable64NameW : XivAlex::GameExecutable32NameW);

			auto params = m_launchParameters;
			if (LanguageRegionModifiable()) {
				auto found = false;
				for (auto& pair : params) {
					if (pair.first == "language") {
						pair.second = std::format("{}", static_cast<int>(m_gameLanguage) - 1);
						found = true;
						break;
					}
				}
				if (!found)
					params.emplace_back("language", std::format("{}", static_cast<int>(m_gameLanguage) - 1));

				found = false;
				for (auto& pair : params) {
					if (pair.first == "SYS.Region") {
						pair.second = std::format("{}", static_cast<int>(m_gameRegion));
						found = true;
						break;
					}
				}
				if (!found)
					params.emplace_back("SYS.Region", std::format("{}", static_cast<int>(m_gameRegion)));
			}

			XivAlexDll::EnableInjectOnCreateProcess(0);
			bool ok;
			if (m_bUseXivAlexander)
				ok = Utils::Win32::RunProgram({
					.path = Dll::Module().PathOf().parent_path() / (m_bUseDirectX11 ? XivAlex::XivAlexLoader64NameW : XivAlex::XivAlexLoader32NameW),
					.args = std::format(L"-a launcher -l select \"{}\" {}", game, XivAlex::CreateGameCommandLine(params, m_bUseParameterObfuscation)),
					.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
					});
			else
				ok = Utils::Win32::RunProgram({
					.path = game,
					.args = Utils::FromUtf8(XivAlex::CreateGameCommandLine(params, m_bUseParameterObfuscation)),
					.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
					});

			if (ok) {
				RemoveTrayIcon();
				process.Terminate(0);
			}

		} catch (const std::exception& e) {
			Utils::Win32::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.GetStringRes(IDS_APP_NAME),
				m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
		}
		XivAlexDll::EnableInjectOnCreateProcess(XivAlexDll::InjectOnCreateProcessAppFlags::Use | XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly);
	}
}

bool App::Window::Main::AskUpdateGameLanguageOverride(App::Config::GameLanguage language) const {
	switch (language) {
		case App::Config::GameLanguage::Unspecified:
			return true;

		case App::Config::GameLanguage::Japanese:
		case App::Config::GameLanguage::English:
		case App::Config::GameLanguage::German:
		case App::Config::GameLanguage::French:
			if (m_sRegion == L"JP")
				return true;
			break;

		case App::Config::GameLanguage::ChineseSimplified:
			if (m_sRegion == L"CN")
				return true;
			break;

		case App::Config::GameLanguage::ChineseTraditional:
			// should not reach here
			return false;

		case App::Config::GameLanguage::Korean:
			if (m_sRegion == L"KR")
				return true;
	}
	return Utils::Win32::MessageBoxF(m_hWnd, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2, L"XivAlexander",
		m_config->Runtime.GetStringRes(IDS_CONFIRM_POSSIBLY_UNSUPPORTED_GAME_CLIENT_LANGUAGE),
		m_config->Runtime.GetLanguageNameLocalized(language)) == IDYES;
}
