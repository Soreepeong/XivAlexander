#include "pch.h"
#include "MainApp/Windows/MainWindow.h"
#include "MainApp/Windows/MainWindow.Internal.h"

#include "Game/CommandLine.h"
#include "Utils/Win32/Resource.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"
#include "MainApp/Features/LoginSessions.h"
#include "MainApp/Features/SocketHook.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Windows/ConfigWindow.h"
#include "MainApp/Windows/SettingsView.h"
#include "MainApp/Windows/ProgressPopupWindow.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"

namespace {
	constexpr UINT WM_COPYGLOBALDATA = 0x0049;

	WNDCLASSEXW WindowClass() {
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
		wcex.lpszClassName = L"XivAlexander::Window::MainWindow";
		wcex.hIconSm = hIcon;
		return wcex;
	}
}

XivAlexander::Apps::MainApp::Window::MainWindow::MainWindow(App& app, std::function<void()> unloadFunction)
	: BaseWindow(WindowClass(), nullptr, WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr)
	, m_app(app)
	, m_triggerUnload(std::move(unloadFunction))
	, m_uTaskbarRestartMessage(RegisterWindowMessageW(L"TaskbarCreated"))
	, m_path(Utils::Win32::Process::Current().PathOf())
	, m_bUseElevation(Utils::Win32::IsUserAnAdmin())
	, m_launchParameters([this]() -> decltype(m_launchParameters) {
	// Already warned about; restarting stays off without them.
	if (!Dll::GetCommandLineError().empty())
		return {};
	try {
		auto res = Game::CommandLine::FromString(Dll::GetOriginalCommandLine(), &m_bUseParameterObfuscation);
		if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
			Game::CommandLine::WellKnown::SetLanguage(res, m_config->Runtime.Launch.RememberedLanguage);
		if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
			Game::CommandLine::WellKnown::SetRegion(res, m_config->Runtime.Launch.RememberedRegion);
		return res;
	} catch (const std::exception& e) {
		m_logger->Format<LogLevel::Warning>(LogCategory::General, m_config->Runtime.GetLangId(), IDS_WARNING_GAME_PARAMETER_PARSE, e.what());
		return {};
	}
		}())
	, m_startupArgumentsForDisplay([this] {
			auto params{ m_launchParameters };
			for (auto& [k, v] : params) {
				if (k == "DEV.TestSID") {
					for (auto& c : v)
						c = '*';
					v += std::format("({})", v.size());
				}
			}
			return Game::CommandLine::ToString(params, false);
		}()) {

	try {
		m_gameReleaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();

		if (m_gameReleaseInfo.Region == xivres::game_release_publisher::SquareEnix && !m_launchParameters.empty()) {
			m_gameLanguage = Game::CommandLine::WellKnown::GetEffectiveLanguage(m_launchParameters);
			m_gameRegion = Game::CommandLine::WellKnown::GetRegion(m_launchParameters, xivres::game_publisher::SquareEnixAmerica);
		} else if (m_gameReleaseInfo.Region == xivres::game_release_publisher::ShengquGames) {
			m_gameLanguage = xivres::game_language::ChineseSimplified;
			m_gameRegion = xivres::game_publisher::ShengquGames;
		} else if (m_gameReleaseInfo.Region == xivres::game_release_publisher::ActozSoft) {
			m_gameLanguage = xivres::game_language::Korean;
			m_gameRegion = xivres::game_publisher::ActozSoft;
		} else if (m_gameReleaseInfo.Region == xivres::game_release_publisher::UserjoyGames) {
			m_gameLanguage = xivres::game_language::TraditionalChinese;
			m_gameRegion = xivres::game_publisher::UserjoyGames;
		}
	} catch (...) {
	}

	RegisterTrayIcon();

	m_settingsView = std::make_unique<SettingsView>(m_app, m_hWnd, m_hWnd,
		[this] { return GetZoom(); },
		[this] { return IsDarkModeEnabled(); },
		[this](HDC hdc, const RECT& rect) { PaintStatus(hdc, rect); },
		SettingsView::RestartChoices{
			.GetState = [this](UINT commandId) { return GetRestartChoiceState(commandId); },
			.Choose = [this](UINT commandId) { ChooseForRestart(commandId); },
		});
	SetWindowPos(m_hWnd, nullptr, 0, 0, static_cast<int>(800 * GetZoom()), static_cast<int>(560 * GetZoom()), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	// Try to restore tray icon every 5 seconds in case things go wrong
	SetTimer(m_hWnd, TimerIdReregisterTrayIcon, 5000, nullptr);

	SetTimer(m_hWnd, TimerIdRepaint, 1000, nullptr);

	m_cleanup += m_config->Runtime.Ui.MainWindow.AlwaysOnTop.OnChange([this] {
		SetWindowPos(m_hWnd, m_config->Runtime.Ui.MainWindow.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		});

	m_cleanup += m_config->Runtime.Ui.MainWindow.Show.OnChange([this] {
		ShowWindow(m_hWnd, m_config->Runtime.Ui.MainWindow.Show ? SW_SHOWNORMAL : SW_HIDE);
		if (m_config->Runtime.Ui.MainWindow.Show)
			SetWindowPos(m_hWnd, m_config->Runtime.Ui.MainWindow.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		});
	if (m_config->Runtime.Ui.MainWindow.Show) {
		// Hidden from view until the posted work before WmUncloak, such as filling the settings tree, has been done.
		BOOL cloak = TRUE;
		DwmSetWindowAttribute(m_hWnd, DWMWA_CLOAK, &cloak, sizeof cloak);
		PostMessageW(m_hWnd, WmUncloak, 0, 0);
		ShowWindow(m_hWnd, SW_SHOW);
		SetWindowPos(m_hWnd, m_config->Runtime.Ui.MainWindow.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (const auto loginSessions = m_app.GetLoginSessions()) {
		m_cleanup += loginSessions->OnChange([this] {
			PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
			});
	}
	m_cleanup += m_config->Runtime.OnVersionSensitiveFeaturesAllowedChange([this] {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.Launch.UseLoginSessionSwitching.OnChange([this] {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.Modding.Ttmp.SearchDirectories.OnChange([this] {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		PostMessageW(m_hWnd, WM_COMMAND, ID_MODDING_TTMP_REFRESH, 0);
		});
	m_cleanup += m_config->Runtime.Modding.GameResourceFileEntryRootDirectories.OnChange([this] {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});

	m_cleanup += m_app.GetSocketHook().OnSocketFound([this](auto&) {
		RunOnUiThread([this] { if (m_settingsView) m_settingsView->InvalidateStatus(); });
		});
	m_cleanup += m_app.GetSocketHook().OnSocketGone([this](auto&) {
		RunOnUiThread([this] { if (m_settingsView) m_settingsView->InvalidateStatus(); });
		});
	ApplyLanguage(m_config->Runtime.GetLangId());

	m_cleanup += m_app.GetResourceOverrider().OnVirtualSqPacksInitialized([this] {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});

	DragAcceptFiles(m_hWnd, TRUE);
	ChangeWindowMessageFilterEx(m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(m_hWnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(m_hWnd, Features::LoginSessions::ReloadMessage, MSGFLT_ALLOW, nullptr);

	if (m_config->Runtime.Opcodes.CheckForUpdatesOnStartup)
		CheckUpdatedOpcodes(false);
}

XivAlexander::Apps::MainApp::Window::MainWindow::~MainWindow() {
	m_cleanupFramerateLockDialog.clear();
	m_cleanup.clear();
	Destroy();
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ApplyLanguage(WORD languageId) {
	m_hAcceleratorWindow = { Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_TRAY_ACCELERATOR), languageId };
	m_hAcceleratorThread = { Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_TRAY_GLOBAL_ACCELERATOR), languageId };
	RepopulateMenu();

	const auto title = std::format(L"{}: {}, {}, {}",
		Dll::GetGenericMessageBoxTitle(), GetCurrentProcessId(), m_gameReleaseInfo.CountryCode, m_gameReleaseInfo.GameVersion);
	SetWindowTextW(m_hWnd, title.c_str());
	if (m_settingsView)
		m_settingsView->ApplyLanguage();
}

bool XivAlexander::Apps::MainApp::Window::MainWindow::IsDialogLike() const {
	// For Tab and Enter in the settings.
	return true;
}

std::vector<std::filesystem::path> XivAlexander::Apps::MainApp::Window::MainWindow::ResolveDirectories(const std::vector<std::filesystem::path>& configured) const {
	std::vector<std::filesystem::path> dirs;
	for (const auto& dir : configured) {
		if (!dir.empty())
			dirs.emplace_back(m_config->TranslateDirectoryPath(dir, m_path.parent_path() / L"sqpack"));
	}
	return dirs;
}

std::filesystem::path XivAlexander::Apps::MainApp::Window::MainWindow::ResolvePrimaryDirectory(const std::vector<std::filesystem::path>& configured, const wchar_t* defaultName) const {
	if (const auto dirs = ResolveDirectories(configured); !dirs.empty())
		return dirs.front();
	return m_config->Init.ResolveConfigStorageDirectoryPath() / defaultName;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::EnsureAndOpenDirectory(const std::filesystem::path& path) {
	if (!exists(path))
		create_directories(path);

	SHELLEXECUTEINFOW se{
		.cbSize = static_cast<DWORD>(sizeof SHELLEXECUTEINFOW),
		.hwnd = m_hWnd,
		.lpVerb = L"explore",
		.lpFile = path.c_str(),
		.nShow = SW_SHOW,
	};
	if (!ShellExecuteExW(&se))
		throw Utils::Win32::Error("ShellExecuteExW");
}
