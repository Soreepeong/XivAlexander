#include "pch.h"
#include "MainApp/Windows/MainWindow.h"
#include "MainApp/Windows/ThemeColors.h"

#include <xivres/packed_stream.model.h>
#include <xivres/packed_stream.placeholder.h>
#include <xivres/packed_stream.standard.h>
#include <xivres/packed_stream.texture.h>
#include <xivres/textools.h>
#include "Game/CommandLine.h"
#include "Utils/Win32/Resource.h"
#include "Utils/Win32/TaskDialogBuilder.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"
#include "MainApp/Features/AudioResampler.h"
#include "MainApp/Features/LoginSessions.h"
#include "MainApp/Features/MainThreadTimingHandler.h"
#include "MainApp/Features/SocketHook.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Modding/VirtualSqPacks.h"
#include "MainApp/Windows/ConfigWindow.h"
#include "MainApp/Windows/SettingsView.h"
#include "MainApp/Windows/ProgressPopupWindow.h"
#include "MainApp/Windows/Dialog/FramerateLockingDialog.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"
#include "Utils/WinHttp.h"

namespace {
	constexpr UINT WM_COPYGLOBALDATA = 0x0049;

	enum : UINT {
		WmTrayCallback = WM_APP + 2,
		WmRepopulateMenu,
	};

	constexpr int TrayItemId = 0x4c19fd7a;
	constexpr int TimerIdReregisterTrayIcon = 100;
	constexpr int TimerIdRepaint = 101;
	constexpr int TimerIdClearCopiedLaunchCommandLine = 102;

	/// Deletes the submenu that has the command among its items.
	bool DeleteSubMenuHolding(HMENU hMenu, UINT commandId) {
		for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
			const auto hSub = GetSubMenu(hMenu, i);
			if (!hSub)
				continue;
			for (int j = 0, count = GetMenuItemCount(hSub); j < count; ++j) {
				if (GetMenuItemID(hSub, j) == commandId) {
					DeleteMenu(hMenu, i, MF_BYPOSITION);
					return true;
				}
			}
			if (DeleteSubMenuHolding(hSub, commandId))
				return true;
		}
		return false;
	}

	/// Deletes the submenus left with nothing but separators.
	void DeleteEmptySubMenus(HMENU hMenu) {
		for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
			const auto hSub = GetSubMenu(hMenu, i);
			if (!hSub)
				continue;
			DeleteEmptySubMenus(hSub);
			auto empty = true;
			for (int j = 0, count = GetMenuItemCount(hSub); empty && j < count; ++j) {
				MENUITEMINFOW mii{.cbSize = sizeof mii, .fMask = MIIM_FTYPE};
				empty = GetMenuItemInfoW(hSub, j, TRUE, &mii) && (mii.fType & MFT_SEPARATOR);
			}
			if (empty)
				DeleteMenu(hMenu, i, MF_BYPOSITION);
		}
	}

	/// Deletes the separators that separate nothing: first, last, or after another.
	void TidySeparators(HMENU hMenu) {
		const auto isSeparator = [hMenu](int i) {
			MENUITEMINFOW mii{.cbSize = sizeof mii, .fMask = MIIM_FTYPE};
			return GetMenuItemInfoW(hMenu, i, TRUE, &mii) && (mii.fType & MFT_SEPARATOR);
		};
		for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; --i) {
			if (const auto hSub = GetSubMenu(hMenu, i))
				TidySeparators(hSub);
			else if (isSeparator(i) && (i == 0 || i == GetMenuItemCount(hMenu) - 1 || isSeparator(i - 1)))
				DeleteMenu(hMenu, i, MF_BYPOSITION);
		}
		if (const auto count = GetMenuItemCount(hMenu); count > 0 && isSeparator(0))
			DeleteMenu(hMenu, 0, MF_BYPOSITION);
	}

	/// Takes out of the menus what is set on the settings pages in the window; their commands and shortcuts still work.
	void TrimMenu(HMENU hMenu) {
		for (const UINT commandId : {
			ID_CONFIGURE_SETTINGS,
			ID_CONFIGURE_EDITOPCODECONFIGURATION,
			ID_CONFIGURE_CHECKFORUPDATEDOPCODES,
			ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP,
			ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING,
			ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE,
			ID_NETWORK_USEIPCTYPEFINDER,
			ID_NETWORK_USEALLIPCMESSAGELOGGER,
			ID_MODDING_TTMP_FLATTENSUBDIRECTORYDISPLAY,
			ID_MODDING_TTMP_USESUBDIRECTORYTOGGLINGONFLATTENEDVIEW,
			ID_MODDING_TTMP_SHOWDEDICATEDMENU,
			ID_MODDING_TTMP_OPENDIRECTORY,
			ID_MODDING_USEALTCODECMUSICSUPPORT,
			ID_MODDING_LOGALLFILEACCESS,
			ID_MODDING_ENABLE,
			ID_NETWORK_REDUCEPACKETDELAY,
			ID_NETWORK_RELEASEALLCONNECTIONS,
			ID_NETWORK_RESETALLCONNECTIONS,
			ID_VIEW_ALWAYSONTOP,
			ID_VIEW_ALWAYSONTOPGAME,
			ID_VIEW_HIDEONMINIMIZE,
		}) {
			while (DeleteMenu(hMenu, commandId, MF_BYCOMMAND)) {}
		}

		// Restart, Modding (whose TexTools ModPacks is the TTMP menu by now), Configure, and Help; and the game fixes, the
		// language, the theme, the window title, the network troubleshooting, the latency and timing helper, the
		// framerate control, and the voice muting.
		for (const UINT commandId : {
			ID_RESTART_RESTART,
			ID_MODDING_TTMP_REFRESH,
			ID_CONFIGURE_RELOAD,
			ID_HELP_OPENHOMEPAGE,
			ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE,
			ID_CONFIGURE_USEMORECPUTIME,
			ID_MODDING_MUTEVOICE_BATTLE,
			ID_CONFIGURE_GAMEFIX_OPENDIRECTORY,
			ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT,
			ID_CONFIGURE_THEME_SYSTEM,
			ID_CONFIGURE_WINDOWTITLE_PID_NONE,
			ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS,
		})
			DeleteSubMenuHolding(hMenu, commandId);

		DeleteEmptySubMenus(hMenu);
		TidySeparators(hMenu);
	}

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

	std::wstring CreateTtmpDirectoryName(const std::string& modPackName, std::wstring fileName) {
		auto name = xivres::util::unicode::convert<std::wstring>(modPackName);
		if (name.empty())
			name = std::move(fileName);
		if (name.empty())
			name = L"Unnamed";
		else
			name = std::format(L"Mod_{}", name);
		name = xivres::util::trim(name);
		for (auto& c : name) {
			if (c == '/' || c == '<' || c == '>' || c == ':' || c == '"' || c == '\\' || c == '|' || c == '?' || c == '*')
				c = '_';
		}
		return name;
	}

	enum class RemoteConfigUpdateResult {
		NotFound,
		NotChanged,
		Changed,
	};

	RemoteConfigUpdateResult UpdateConfigFromRemote(XivAlexander::BaseConfigRepository& repository, const std::string& url) {
		std::string prev;
		try {
			const auto prevFile = Utils::Win32::Handle::FromCreateFile(repository.GetConfigPath(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
			prev.resize(prevFile.GetFileSize());
			prevFile.Read(0, prev.data(), prev.size());
			prev = nlohmann::json::parse(prev).dump();
		} catch (...) {
			prev.clear();
		}

		std::string updated;
		{
			const auto response = Utils::Win32::WinHttp::Get(url);

			switch (static_cast<int>(response.StatusCode)) {
				case 200:
					updated = nlohmann::json::parse(response.Body).dump();
					break;

				case 404:
					return RemoteConfigUpdateResult::NotFound;

				default:
					throw std::runtime_error(std::format("HTTP Error {}", response.StatusCode));
			}
		}

		if (updated == prev)
			return RemoteConfigUpdateResult::NotChanged;

		std::ofstream(repository.GetConfigPath()) << updated;
		repository.Reload();
		return RemoteConfigUpdateResult::Changed;
	}

	size_t UpdatePatchCodesFromRemote(const std::filesystem::path& directory) {
		const auto listing = Utils::Win32::WinHttp::Get("https://api.github.com/repos/Soreepeong/XivAlexander/contents/StaticData/PatchCode?ref=main");
		if (listing.StatusCode != 200)
			throw std::runtime_error(std::format("HTTP Error {}", listing.StatusCode));

		size_t changed = 0;
		for (const auto& item : nlohmann::json::parse(listing.Body)) {
			const auto name = item.value("name", "");
			if (item.value("type", "") != "file" || !name.ends_with(".json") || name.find_first_of("/\\:") != std::string::npos)
				continue;

			const auto response = Utils::Win32::WinHttp::Get(item.at("download_url").get<std::string>());
			if (response.StatusCode != 200)
				throw std::runtime_error(std::format("{}: HTTP Error {}", name, response.StatusCode));
			const auto updated = nlohmann::json::parse(response.Body);

			const auto path = directory / xivres::util::unicode::convert<std::wstring>(name);
			try {
				if (Utils::ParseJsonFromFile(path) == updated)
					continue;
			} catch (...) {
				// missing or broken; overwrite
			}
			Utils::SaveJsonToFile(path, updated);
			changed++;
		}
		return changed;
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
			m_gameLanguage = Game::CommandLine::WellKnown::GetLanguage(m_launchParameters, xivres::game_language::English);
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
		},
		[this](Features::Modding::NestedTtmp& folder, UINT commandId) { BatchTtmpOperation(folder, static_cast<int>(commandId)); });
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
		ShowWindow(m_hWnd, SW_SHOW);
		SetWindowPos(m_hWnd, m_config->Runtime.Ui.MainWindow.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (auto& loginSessions = m_app.GetLoginSessions()) {
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

void XivAlexander::Apps::MainApp::Window::MainWindow::ShowContextMenu(const BaseWindow* parent) const {
	if (!parent)
		parent = this;

	SetMenuStates();
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
			parent->Handle(),
			nullptr
		);
	}

	if (result)
		SendMessageW(m_hWnd, WM_COMMAND, MAKEWPARAM(result, 0), 0);
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

void XivAlexander::Apps::MainApp::Window::MainWindow::OnLayout(double zoom, double width, double height, int resizeType) {
	if (m_settingsView)
		SetWindowPos(m_settingsView->Handle(), nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), SWP_NOZORDER | SWP_NOACTIVATE);
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
		if (auto& handler = m_app.GetMainThreadTimingHelper()) {
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
			if (it != profiles.end())
				ttmpProfile = m_config->Runtime.FormatStringRes(IDS_STATUS_TTMPPROFILE, xivres::util::unicode::convert<std::wstring>(it->Name.empty() ? it->FileName : it->Name));
		}
		str = m_config->Runtime.FormatStringRes(IDS_MAIN_TEXT,
			msgPumpMean, msgPumpDev, msgPumpCount,
			ttmpProfile,
			m_app.GetSocketHook().Describe());
		if (m_config->Runtime.AreVersionSensitiveFeaturesDisabledTemporarily())
			str = std::format(L"{}\n{}", m_config->Runtime.GetStringRes(IDS_VERSIONSENSITIVE_DISABLEDUNTILRESTART), str);
	} catch (...) {
		// pass
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

LRESULT XivAlexander::Apps::MainApp::Window::MainWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (lParam == 1) {
			DestroyWindow(m_hWnd);
		} else if (lParam == 2) {
			RemoveTrayIcon();
			TerminateProcess(GetCurrentProcess(), static_cast<UINT>(wParam));
		} else {
			switch (Dll::MessageBoxF(m_hWnd, MB_YESNOCANCEL | MB_ICONQUESTION, m_config->Runtime.FormatStringRes(IDS_CONFIRM_MAIN_WINDOW_CLOSE,
				Utils::Win32::MB_GetString(IDYES - 1),
				Utils::Win32::MB_GetString(IDNO - 1),
				Utils::Win32::MB_GetString(IDCANCEL - 1)
			))) {
				case IDYES:
					m_triggerUnload();
					break;
				case IDNO:
					m_config->Runtime.Ui.MainWindow.Show = false;
					break;
			}
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
		auto& loginSessions = m_app.GetLoginSessions();
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
				if (m_menuIdCallbacks.contains(menuId)) {
					m_menuIdCallbacks[menuId]();
				} else {
					OnCommand_Menu_File(menuId);
					OnCommand_Menu_Restart(menuId);
					OnCommand_Menu_Network(menuId);
					OnCommand_Menu_Modding(menuId);
					OnCommand_Menu_Configure(menuId);
					OnCommand_Menu_View(menuId);
					OnCommand_Menu_Help(menuId);
				}
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
			m_config->Runtime.Ui.MainWindow.Show = false;
			return 0;
		}
	}
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
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

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu() {
	if (GetWindowThreadProcessId(m_hWnd, nullptr) != GetCurrentThreadId()) {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		return;
	}

	auto menu = Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_TRAY_MENU), m_config->Runtime.GetLangId());

	const auto title = std::format(L"{}: {}, {}, {}",
		m_config->Runtime.GetStringRes(IDS_APP_NAME), GetCurrentProcessId(), m_gameReleaseInfo.CountryCode, m_gameReleaseInfo.GameVersion);
	ModifyMenuW(menu, ID_FILE_CURRENTINFO, MF_BYCOMMAND, ID_FILE_CURRENTINFO, title.c_str());

	if (m_config->Runtime.AreVersionSensitiveFeaturesDisabledTemporarily()) {
		for (const UINT id : {ID_MODDING_ENABLE, ID_MODDING_USEALTCODECMUSICSUPPORT}) {
			const auto label = std::format(L"(!) {}", RepopulateMenu_GetMenuTextById(menu, id).c_str());
			ModifyMenuW(menu, id, MF_BYCOMMAND | MF_STRING, id, label.c_str());
		}
	}
	if (!m_app.GetResourceOverrider().IsActive()) {
		ModifyMenuW(menu, ID_MODDING_ANYTHINGBELOWHEREWILLBEAPPLIEDONRESTART, MF_BYCOMMAND | MF_STRING | MF_DISABLED, ID_MODDING_ANYTHINGBELOWHEREWILLBEAPPLIEDONRESTART,
			m_config->Runtime.GetStringRes(IDS_MENU_MODDING_REQUIRESENABLEANDRESTART));
	}

	m_menuIdCallbacks.clear();

	TrimMenu(menu);
	menu.AttachAndSwap(m_hWnd);
}

UINT_PTR XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_AllocateMenuId(std::function<void()> cb) {
	const auto hMenu = GetMenu(m_hWnd);
	uint16_t counter = 50000;

	MENUITEMINFOW mii = {
		.cbSize = sizeof mii,
		.fMask = MIIM_STATE,
	};

	while (m_menuIdCallbacks.contains(counter) || GetMenuItemInfoW(hMenu, counter, MF_BYCOMMAND, &mii))
		++counter;
	m_menuIdCallbacks.emplace(counter, std::move(cb));
	return counter;
}

std::wstring XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_GetMenuTextById(HMENU hParentMenu, UINT commandId) {
	std::wstring res(static_cast<size_t>(1) + GetMenuStringW(hParentMenu, commandId, nullptr, 0, MF_BYCOMMAND), '\0');
	GetMenuStringW(hParentMenu, commandId, res.data(), static_cast<int>(res.size()), MF_BYCOMMAND);
	return res;
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
	// The first listed, where new files go; the default, even if not listed, if none is.
	if (const auto dirs = ResolveDirectories(configured); !dirs.empty())
		return dirs.front();
	return m_config->Init.ResolveConfigStorageDirectoryPath() / defaultName;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::SetMenuStates() const {
	const auto hMenu = GetMenu(m_hWnd);

	const auto& config = m_config->Runtime;
	using namespace Utils::Win32;

	// File
	{
		SetMenuState(hMenu, ID_FILE_SHOWCONTROLWINDOW, config.Ui.MainWindow.Show, true);
		SetMenuState(hMenu, ID_FILE_SHOWLOGGINGWINDOW, config.Ui.LogWindow.Show, true);
	}

	// Network
	{
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE, config.NetworkTiming.Enabled, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1, config.NetworkTiming.HighLatencyMitigationMode == HighLatencyMitigationMode::SubtractLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2, config.NetworkTiming.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateRtt, true, config.FormatStringRes(IDS_MENU_NETWORKLATENCYHANDLEMODE_2, config.NetworkTiming.ExpectedAnimationLockDurationUs.Value()));
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3, config.NetworkTiming.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateNormalizedRttAndLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING, config.NetworkTiming.UseHighLatencyMitigationLogging, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE, config.NetworkTiming.UseHighLatencyMitigationPreviewMode, true);
		SetMenuState(hMenu, ID_NETWORK_USEIPCTYPEFINDER, config.Opcodes.UseOpcodeFinder, true);
		SetMenuState(hMenu, ID_NETWORK_USEALLIPCMESSAGELOGGER, config.Opcodes.UseAllIpcMessageLogger, true);
		SetMenuState(hMenu, ID_NETWORK_REDUCEPACKETDELAY, config.Socket.ReducePacketDelay, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES, config.Socket.TakeOverLoopbackAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES, config.Socket.TakeOverPrivateAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES, config.Socket.TakeOverAllAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS, config.Socket.TakeOverAllPorts, true);
	}

	// Modding
	{
		SetMenuState(hMenu, ID_MODDING_ENABLE, config.Modding.Enabled, true);
		SetMenuState(hMenu, ID_MODDING_USEALTCODECMUSICSUPPORT, config.Modding.UseAltCodecMusicSupport, true);
		SetMenuState(hMenu, ID_MODDING_LOGALLFILEACCESS, config.Modding.Logging.AllDataFileRead, true);

		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_BATTLE, config.Modding.MuteVoice.Battle, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_CM, config.Modding.MuteVoice.Cm, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_EMOTE, config.Modding.MuteVoice.Emote, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_LINE, config.Modding.MuteVoice.Line, true);

	}

	// Configure
	{
		SetMenuState(hMenu, ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP, config.Opcodes.CheckForUpdatesOnStartup, true);
		SetMenuState(hMenu, ID_CONFIGURE_USEMORECPUTIME, config.FramerateControl.UseMoreCpuTime, true);
		SetMenuState(hMenu, ID_CONFIGURE_BACKGROUND_FRAMERATE_LIMIT, config.FramerateControl.UseBackgroundLimit, true, config.FormatStringRes(IDS_MENU_BACKGROUND_FRAMERATE_TARGET, config.FramerateControl.BackgroundLimit.Value()));
		if (config.FramerateControl.Lock.Automatic)
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, true, true, m_config->Runtime.GetStringRes(IDS_MENU_LOCKFRAMERATE_AUTOMATIC));
		else if (config.FramerateControl.Lock.Interval)
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, true, true, m_config->Runtime.FormatStringRes(IDS_MENU_LOCKFRAMERATE, 1000000. / config.FramerateControl.Lock.Interval));
		else
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, false, true, m_config->Runtime.GetStringRes(IDS_MENU_LOCKFRAMERATE_DISABLED));
		SetMenuState(hMenu, ID_CONFIGURE_SYNCHRONIZEPROCESSING, config.FramerateControl.SynchronizeProcessing, true);
		SetMenuState(hMenu, ID_CONFIGURE_WINDOWTITLE_PID_NONE, config.GameWindow.TitleMode == GameWindowTitleMode::None, true);
		SetMenuState(hMenu, ID_CONFIGURE_WINDOWTITLE_PID_PREFIX, config.GameWindow.TitleMode == GameWindowTitleMode::Prefix, true);
		SetMenuState(hMenu, ID_CONFIGURE_WINDOWTITLE_PID_SUFFIX, config.GameWindow.TitleMode == GameWindowTitleMode::Suffix, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT, config.Ui.Language == Language::SystemDefault, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_ENGLISH, config.Ui.Language == Language::English, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_KOREAN, config.Ui.Language == Language::Korean, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_JAPANESE, config.Ui.Language == Language::Japanese, true);
		SetMenuState(hMenu, ID_CONFIGURE_THEME_SYSTEM, config.Ui.ThemeMode == ThemeMode::System, true);
		SetMenuState(hMenu, ID_CONFIGURE_THEME_LIGHT, config.Ui.ThemeMode == ThemeMode::Light, true);
		SetMenuState(hMenu, ID_CONFIGURE_THEME_DARK, config.Ui.ThemeMode == ThemeMode::Dark, true);
	}

	// View
	{
		SetMenuState(hMenu, ID_VIEW_ALWAYSONTOP, config.Ui.MainWindow.AlwaysOnTop, true);
		SetMenuState(hMenu, ID_VIEW_ALWAYSONTOPGAME, config.GameWindow.AlwaysOnTop, true);
		SetMenuState(hMenu, ID_VIEW_HIDEONMINIMIZE, config.Ui.MainWindow.HideOnMinimize, true);
	}
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

void XivAlexander::Apps::MainApp::Window::MainWindow::RemoveTrayIcon() {
	NOTIFYICONDATAW nid{.cbSize = sizeof nid};
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

std::filesystem::path XivAlexander::Apps::MainApp::Window::MainWindow::GameExecutablePath() {
	return Utils::Win32::Process::Current().PathOf().parent_path() / Dll::GameExecutable64NameW;
}

std::wstring XivAlexander::Apps::MainApp::Window::MainWindow::MakeLaunchArguments() const {
	auto params{ m_launchParameters };
	if (auto& loginSessions = m_app.GetLoginSessions())
		loginSessions->ApplySelectedTo(params);
	if (Dll::IsLanguageRegionModifiable()) {
		Game::CommandLine::WellKnown::SetLanguage(params, m_gameLanguage);
		Game::CommandLine::WellKnown::SetRegion(params, m_gameRegion);
	}
	return Game::CommandLine::ToString(params, m_bUseParameterObfuscation);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::CopyLaunchCommandLine() {
	const auto text = std::format(L"{} {}", Utils::Win32::ReverseCommandLineToArgv(GameExecutablePath().wstring()), MakeLaunchArguments());

	const auto allocGlobal = [](const void* data, size_t bytes) {
		const auto hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (!hMem)
			throw Utils::Win32::Error("GlobalAlloc");
		if (const auto p = GlobalLock(hMem)) {
			memcpy(p, data, bytes);
			GlobalUnlock(hMem);
		} else {
			const auto error = Utils::Win32::Error("GlobalLock");
			GlobalFree(hMem);
			throw error;
		}
		return hMem;
	};

	const auto hMem = allocGlobal(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
	if (!OpenClipboard(m_hWnd)) {
		const auto error = Utils::Win32::Error("OpenClipboard");
		GlobalFree(hMem);
		throw error;
	}
	EmptyClipboard();
	if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
		const auto error = Utils::Win32::Error("SetClipboardData");
		CloseClipboard();
		GlobalFree(hMem);
		throw error;
	}

	// treat as password like what KeePass 2.x does
	const auto setMarker = [&allocGlobal](const wchar_t* formatName, const void* data, size_t bytes) {
		const auto format = RegisterClipboardFormatW(formatName);
		if (!format)
			return;
		try {
			const auto hMarker = allocGlobal(data, bytes);
			if (!SetClipboardData(format, hMarker))
				GlobalFree(hMarker);
		} catch (...) {
			// pass
		}
	};
	static constexpr wchar_t ViewerIgnoreValue[] = L"XivAlexander";
	static constexpr DWORD True = 1, False = 0;
	setMarker(L"Clipboard Viewer Ignore", ViewerIgnoreValue, sizeof ViewerIgnoreValue);
	setMarker(L"ExcludeClipboardContentFromMonitorProcessing", &True, sizeof True);
	setMarker(L"CanUploadToCloudClipboard", &False, sizeof False);
	setMarker(L"CanIncludeInClipboardHistory", &False, sizeof False);
	CloseClipboard();

	if (const auto clearSeconds = m_config->Runtime.Launch.ClearCopiedCommandLineSeconds.Value()) {
		m_copiedLaunchCommandLineClipboardSequence = GetClipboardSequenceNumber();
		SetTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine, clearSeconds * 1000, nullptr);
	} else {
		m_copiedLaunchCommandLineClipboardSequence = 0;
		KillTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine);
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ClearCopiedLaunchCommandLine() {
	if (!m_copiedLaunchCommandLineClipboardSequence)
		return;

	if (GetClipboardSequenceNumber() == m_copiedLaunchCommandLineClipboardSequence) {
		if (!OpenClipboard(m_hWnd)) {
			SetTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine, 1000, nullptr);
			return;
		}
		EmptyClipboard();
		CloseClipboard();
	}

	m_copiedLaunchCommandLineClipboardSequence = 0;
	KillTimer(m_hWnd, TimerIdClearCopiedLaunchCommandLine);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::AskRestartGame(bool onlyOnModifier) {
	if (onlyOnModifier && !((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))) {
		return;
	}
	const auto yes = Utils::Win32::MB_GetString(IDYES - 1);
	const auto no = Utils::Win32::MB_GetString(IDNO - 1);
	auto content = m_config->Runtime.FormatStringRes(
		IDS_CONFIRM_RESTART_GAME,
		m_bUseXivAlexander ? yes : no,
		m_bUseParameterObfuscation ? yes : no,
		m_bUseElevation ? yes : no,
		m_config->Runtime.GetLanguageNameLocalized(m_gameLanguage),
		m_config->Runtime.GetRegionNameLocalized(m_gameRegion));
	if (const auto& loginSessions = m_app.GetLoginSessions()) {
		const auto sessions = loginSessions->GetSessions();
		if (const auto selected = loginSessions->GetSelectedIndex(); selected < sessions.size()) {
			content += m_config->Runtime.FormatStringRes(IDS_CONFIRM_RESTART_GAME_LOGINSESSION, sessions[selected].Alias.empty()
				? std::wstring(m_config->Runtime.GetStringRes(IDS_MENU_LOGINSESSION_LAUNCHARGUMENTS))
				: xivres::util::unicode::convert<std::wstring>(sessions[selected].Alias));
		}
	}

	static constexpr int IdRestart = 1001;
	static constexpr int IdNewInstance = 1002;
	const auto choice = Utils::Win32::TaskDialog::Builder()
		.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
		.WithParentWindow(m_hWnd)
		.WithInstance(Dll::Module())
		.WithAllowDialogCancellation()
		.WithMainIcon(IDI_TRAY_ICON)
		.WithMainInstruction(std::wstring(m_config->Runtime.GetStringRes(IDS_CONFIRM_RESTART_GAME_TITLE)))
		.WithContent(content)
		.WithButton({.IdSet = true, .Id = IdRestart, .Text = std::wstring(m_config->Runtime.GetStringRes(IDS_RESTART_GAME_RESTART))})
		.WithButton({.IdSet = true, .Id = IdNewInstance, .Text = std::wstring(m_config->Runtime.GetStringRes(IDS_RESTART_GAME_NEWINSTANCE))})
		.WithCommonButton(TDCBF_CANCEL_BUTTON)
		.WithButtonCommandLinks()
		.WithButtonDefault(IdRestart)
		.Build()
		.Show()
		.Button;
	if (choice == IdRestart || choice == IdNewInstance) {
		Utils::Win32::RunProgramParams runParams{
			.path = GameExecutablePath(),
			.args = MakeLaunchArguments(),
			.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
		};

		Dll::EnableInjectOnCreateProcess(0);
		const auto revertInjectOnCreateProcess = xivres::util::on_dtor([] { Dll::EnableInjectOnCreateProcess(Dll::InjectOnCreateProcessAppFlags::Use | Dll::InjectOnCreateProcessAppFlags::InjectGameOnly); });

		// This only mattered on initialization at AutoLoadAsDependencyModule, so it's safe to modify and not revert
		if (m_bUseXivAlexander)
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);
		else
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", L"1");

		if (!Dll::IsLoadedAsDependency() && m_bUseXivAlexander) {
			runParams.args = std::format(L"-a launcher -l select {} {}", Utils::Win32::ReverseCommandLineToArgv(runParams.path), runParams.args);
			runParams.path = Dll::Module().PathOf().parent_path() / Dll::XivAlexLoader64NameW;
		}

		if (Utils::Win32::RunProgram(std::move(runParams)) && choice == IdRestart) {
			RemoveTrayIcon();
			Utils::Win32::Process::Current().Terminate(0);
		}
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_File(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_GLOBAL_SHOW_TRAYMENU:
			for (const auto& w : All())
				if (w->Handle() == GetForegroundWindow())
					ShowContextMenu(w);
			return;

		case ID_FILE_CURRENTINFO:
			if (const auto hWnd = this->m_app.GetGameWindowHandle())
				SetForegroundWindow(hWnd);
			return;

		case ID_FILE_SHOWLOGGINGWINDOW:
			config.Ui.LogWindow.Show.Toggle();
			return;

		case ID_FILE_SHOWCONTROLWINDOW:
			config.Ui.MainWindow.Show.Toggle();
			SetForegroundWindow(m_hWnd);
			return;

		case ID_FILE_CHECKFORUPDATES:
			LaunchXivAlexLoaderWithTargetHandles({ Utils::Win32::Process::Current() },
				Dll::GetUnloadDisabledReason() ? Dll::LoaderAction::UpdateCheck : Dll::LoaderAction::Internal_Update_DependencyDllMode,
				false);
			return;

		case ID_FILE_UNLOADXIVALEXANDER:
			m_triggerUnload();
			return;

		case ID_FILE_FORCEEXITGAME:
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_CONFIRM_EXIT_GAME)) == IDYES) {
				RemoveTrayIcon();
				TerminateProcess(GetCurrentProcess(), 0);
			}
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Restart(int menuId) {
	switch (menuId) {
		case ID_RESTART_RESTART:
			AskRestartGame();
			return;

		case ID_RESTART_COPYLAUNCHCOMMANDLINE:
			CopyLaunchCommandLine();
			return;

		default:
			if (ChooseForRestart(menuId))
				AskRestartGame(true);
			return;
	}
}

bool XivAlexander::Apps::MainApp::Window::MainWindow::ChooseForRestart(UINT commandId) {
	// Whether the launch changed, rather than what is remembered of it.
	switch (commandId) {
		case ID_RESTART_USEXIVALEXANDER:
			m_bUseXivAlexander = !m_bUseXivAlexander;
			return true;

		case ID_RESTART_USEPARAMETEROBFUSCATION:
			m_bUseParameterObfuscation = !m_bUseParameterObfuscation;
			return true;

		case ID_RESTART_USEELEVATION:
			m_bUseElevation = !m_bUseElevation;
			return true;

		case ID_RESTART_LANGUAGE_REMEMBER:
			if (m_config->Runtime.Launch.RememberedLanguage == xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			else
				m_config->Runtime.Launch.RememberedLanguage = xivres::game_language::Unspecified;
			return false;

		case ID_RESTART_LANGUAGE_ENGLISH:
			m_gameLanguage = xivres::game_language::English;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_GERMAN:
			m_gameLanguage = xivres::game_language::German;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_FRENCH:
			m_gameLanguage = xivres::game_language::French;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_JAPANESE:
			m_gameLanguage = xivres::game_language::Japanese;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE:
			m_gameLanguage = xivres::game_language::ChineseSimplified;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_KOREAN:
			m_gameLanguage = xivres::game_language::Korean;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_LANGUAGE_CHINESETRADITIONAL:
			m_gameLanguage = xivres::game_language::TraditionalChinese;
			if (m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified)
				m_config->Runtime.Launch.RememberedLanguage = m_gameLanguage;
			return true;

		case ID_RESTART_REGION_REMEMBER:
			if (m_config->Runtime.Launch.RememberedRegion == xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			else
				m_config->Runtime.Launch.RememberedRegion = xivres::game_publisher::Unspecified;
			return false;

		case ID_RESTART_REGION_JAPAN:
			m_gameRegion = xivres::game_publisher::SquareEnixJapan;
			if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			return true;

		case ID_RESTART_REGION_NORTH_AMERICA:
			m_gameRegion = xivres::game_publisher::SquareEnixAmerica;
			if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			return true;

		case ID_RESTART_REGION_EUROPE:
			m_gameRegion = xivres::game_publisher::SquareEnixEurope;
			if (m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified)
				m_config->Runtime.Launch.RememberedRegion = m_gameRegion;
			return true;
	}
	return false;
}

std::pair<bool, bool> XivAlexander::Apps::MainApp::Window::MainWindow::GetRestartChoiceState(UINT commandId) const {
	// Whether it is chosen, and whether it can be changed.
	const auto languageRegionModifiable = Dll::IsLanguageRegionModifiable();
	switch (commandId) {
		case ID_RESTART_USEXIVALEXANDER: return {m_bUseXivAlexander, !m_launchParameters.empty()};
		case ID_RESTART_USEPARAMETEROBFUSCATION: return {m_bUseParameterObfuscation, !m_launchParameters.empty()};
		case ID_RESTART_USEELEVATION: return {m_bUseElevation, !m_launchParameters.empty()};
		case ID_RESTART_LANGUAGE_REMEMBER: return {languageRegionModifiable && m_config->Runtime.Launch.RememberedLanguage != xivres::game_language::Unspecified, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_ENGLISH: return {m_gameLanguage == xivres::game_language::English, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_GERMAN: return {m_gameLanguage == xivres::game_language::German, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_FRENCH: return {m_gameLanguage == xivres::game_language::French, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_JAPANESE: return {m_gameLanguage == xivres::game_language::Japanese, languageRegionModifiable};
		case ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE: return {m_gameLanguage == xivres::game_language::ChineseSimplified, false};
		case ID_RESTART_LANGUAGE_KOREAN: return {m_gameLanguage == xivres::game_language::Korean, false};
		case ID_RESTART_LANGUAGE_CHINESETRADITIONAL: return {m_gameLanguage == xivres::game_language::TraditionalChinese, false};
		case ID_RESTART_REGION_REMEMBER: return {languageRegionModifiable && m_config->Runtime.Launch.RememberedRegion != xivres::game_publisher::Unspecified, languageRegionModifiable};
		case ID_RESTART_REGION_JAPAN: return {m_gameRegion == xivres::game_publisher::SquareEnixJapan, languageRegionModifiable};
		case ID_RESTART_REGION_NORTH_AMERICA: return {m_gameRegion == xivres::game_publisher::SquareEnixAmerica, languageRegionModifiable};
		case ID_RESTART_REGION_EUROPE: return {m_gameRegion == xivres::game_publisher::SquareEnixEurope, languageRegionModifiable};
		default: return {false, false};
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Network(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE:
			config.NetworkTiming.Enabled.Toggle();
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1:
			config.NetworkTiming.HighLatencyMitigationMode = HighLatencyMitigationMode::SubtractLatency;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2:
			config.NetworkTiming.HighLatencyMitigationMode = HighLatencyMitigationMode::SimulateRtt;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3:
			config.NetworkTiming.HighLatencyMitigationMode = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING:
			config.NetworkTiming.UseHighLatencyMitigationLogging.Toggle();
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE:
			config.NetworkTiming.UseHighLatencyMitigationPreviewMode.Toggle();
			return;

		case ID_NETWORK_USEALLIPCMESSAGELOGGER:
			config.Opcodes.UseAllIpcMessageLogger.Toggle();
			return;

		case ID_NETWORK_USEIPCTYPEFINDER:
			config.Opcodes.UseOpcodeFinder.Toggle();
			return;

		case ID_NETWORK_REDUCEPACKETDELAY:
			config.Socket.ReducePacketDelay.Toggle();
			return;

		case ID_NETWORK_RELEASEALLCONNECTIONS:
			m_app.RunOnGameLoop([this] { m_app.GetSocketHook().ReleaseSockets(); });
			return;

		case ID_NETWORK_RESETALLCONNECTIONS:
			m_app.RunOnGameLoop([this] { m_app.GetSocketHook().ResetAllConnections(); });
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES:
			config.Socket.TakeOverLoopbackAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES:
			config.Socket.TakeOverPrivateAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES:
			config.Socket.TakeOverAllAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS:
			config.Socket.TakeOverAllPorts.Toggle();
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Modding(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_MODDING_ENABLE:
			config.Modding.Enabled.Toggle();
			return;

		case ID_MODDING_USEALTCODECMUSICSUPPORT:
			config.Modding.UseAltCodecMusicSupport.Toggle();
			return;

		case ID_MODDING_LOGALLFILEACCESS:
			config.Modding.Logging.AllDataFileRead.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_BATTLE:
			config.Modding.MuteVoice.Battle.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_CM:
			config.Modding.MuteVoice.Cm.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_EMOTE:
			config.Modding.MuteVoice.Emote.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_LINE:
			config.Modding.MuteVoice.Line.Toggle();
			return;

		case ID_MODDING_TTMP_IMPORT: {
			const COMDLG_FILTERSPEC fileTypes[] = {
				{.pszName = m_config->Runtime.GetStringRes(IDS_FILTERSPEC_TTMP), .pszSpec = L"*.ttmp; *.ttmp2; *.mpl"},
				{.pszName = m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), .pszSpec = L"*"},
			};
			const auto paths = ChooseFileToOpen(std::span(fileTypes), IDS_TITLE_IMPORT_TTMP);
			switch (paths.size()) {
				case 0:
					return;

				case 1: {
					ProgressPopupWindow progress(m_hWnd);
					progress.UpdateMessage(xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_TITLE_IMPORTING)));
					progress.Show();

					std::string resultMessage;
					const auto adderThread = Utils::Win32::Thread(L"TTMP Importer", [&] {
						try {
							resultMessage = InstallTTMP(paths[0], progress);
						} catch (const std::exception& e) {
							progress.Cancel();
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						});

					while (WAIT_TIMEOUT == progress.DoModalLoop(100, { adderThread })) {
						// pass
					}
					adderThread.Wait();

					if (!resultMessage.empty())
						Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONINFORMATION, xivres::util::unicode::convert<std::wstring>(resultMessage));
					return;
				}

				default:
					InstallMultipleFiles(paths);
			}

			return;
		}

		case ID_MODDING_TTMP_REMOVEALL:
		case ID_MODDING_TTMP_ENABLEALL:
		case ID_MODDING_TTMP_DISABLEALL: {
			if (const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks())
				BatchTtmpOperation(*sqpacks->GetTtmps(), menuId);
			return;
		}

		case ID_MODDING_TTMP_OPENDIRECTORY:
			EnsureAndOpenDirectory(ResolvePrimaryDirectory(m_config->Runtime.Modding.Ttmp.SearchDirectories.Value(), L"TexToolsMods"));
			return;

		case ID_MODDING_TTMP_REFRESH: {
			if (m_backgroundWorkerThread) {
				m_ttmpRescanPending = true;
				return;
			}
			if (auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks()) {
				m_backgroundWorkerThread = Utils::Win32::Thread(L"RescanTtmpOnOtherThread", [this, &sqpacks]{
					m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);
					const auto workerThread = Utils::Win32::Thread(L"RescanTtmp", [&] {
						sqpacks->RescanTtmp(*m_backgroundWorkerProgressWindow);
						});

					do {
						m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
						// A rescan is usually quick enough that showing this at once only flashes it.
						m_backgroundWorkerProgressWindow->Show(std::chrono::seconds(3));
					} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
					workerThread.Wait();
					m_backgroundWorkerThread = nullptr;
					m_backgroundWorkerProgressWindow = nullptr;
					if (m_ttmpRescanPending.exchange(false))
						PostMessageW(m_hWnd, WM_COMMAND, ID_MODDING_TTMP_REFRESH, 0);
					});
			}
			return;
		}

		case ID_MODDING_OPENREPLACEMENTFILEENTRIESDIRECTORY:
			EnsureAndOpenDirectory(ResolvePrimaryDirectory(m_config->Runtime.Modding.GameResourceFileEntryRootDirectories.Value(), L"ReplacementFileEntries"));
			return;

		case ID_MODDING_EXPORTTOTTMP: {
			if (m_backgroundWorkerThread) {
				const auto window = decltype(m_backgroundWorkerProgressWindow)(m_backgroundWorkerProgressWindow);
				if (window && window->GetCancelEvent().Wait(0) == WAIT_TIMEOUT)
					SetForegroundWindow(m_backgroundWorkerProgressWindow->Handle());
				else
					Dll::MessageBoxF(m_hWnd, MB_ICONWARNING, IDS_ERROR_CANCELLING_TRYAGAINLATER);
				return;
			}

			std::filesystem::path targetDir;
			try {
				IFileOpenDialogPtr pDialog;
				DWORD dwFlags;
				Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(m_config->Runtime.GetStringRes(IDS_TITLE_EXPORTTTMPDIRECTORY)));
				Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS));
				Utils::Win32::Error::ThrowIfFailed(pDialog->Show(m_hWnd), true);

				IShellItemPtr pResult;
				PWSTR pszFileName;
				Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
				Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
				if (!pszFileName)
					throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
				const auto freeFileName = xivres::util::on_dtor([pszFileName] { CoTaskMemFree(pszFileName); });

				targetDir = pszFileName;

			} catch (const Utils::Win32::CancelledError&) {
				return;

			} catch (const std::exception& e) {
				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
				return;
			}
			auto ttmpl = Utils::Win32::Handle::FromCreateFile(targetDir / "TTMPL.mpl", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);
			auto ttmpd = Utils::Win32::Handle::FromCreateFile(targetDir / "TTMPD.mpd", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0);

			m_backgroundWorkerThread = Utils::Win32::Thread(L"TtmpExporterOnOtherThread", [this, ttmpl = std::move(ttmpl), ttmpd = std::move(ttmpd)]{
				uint64_t ttmplPtr = 0, ttmpdPtr = 0;

				m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);

				size_t index = 0;
				size_t count = 0;
				std::vector<std::pair<std::filesystem::path, std::wstring>> worklist;
				const std::wstring * pLastStartedTargetFile = nullptr;

				const auto workerThread = Utils::Win32::Thread(L"TtmpExporter", [&] {
					const auto targetBasePath = ResolvePrimaryDirectory(m_config->Runtime.Modding.GameResourceFileEntryRootDirectories.Value(), L"ReplacementFileEntries");
					try {
						for (const auto& target : std::filesystem::recursive_directory_iterator(targetBasePath))
							if (target.is_regular_file()) {
								worklist.emplace_back(target.path(), relative(target.path(), targetBasePath).wstring());
								for (auto& c : worklist.back().second) {
									if (c == L'\\')
										c = '/';
								}
							}
					} catch (const std::exception& e) {
						Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						return;
					}

					count = worklist.size();

					for (const auto& [target, relPath] : worklist) {
						pLastStartedTargetFile = &relPath;
						auto extensionLower = std::filesystem::path(relPath).extension().wstring();
						CharLowerW(&extensionLower[0]);
						try {
							const auto entryPathSpec = xivres::path_spec(relPath);
							const auto datFile = entryPathSpec.packname();
							if (datFile.empty())
								throw std::runtime_error(std::format("Could not decide where to store {}", relPath));

							std::vector<char> dv;
							if (file_size(target) == 0)
								dv = xivres::placeholder_packed_stream(entryPathSpec).read_vector<char>();
							else if (extensionLower == L".tex")
								dv = xivres::compressing_packed_stream<xivres::texture_compressing_packer>(entryPathSpec, std::make_shared<xivres::file_stream>(target), Z_BEST_COMPRESSION).read_vector<char>();
							else if (extensionLower == L".mdl")
								dv = xivres::compressing_packed_stream<xivres::model_compressing_packer>(entryPathSpec, std::make_shared<xivres::file_stream>(target), Z_BEST_COMPRESSION).read_vector<char>();
							else
								dv = xivres::compressing_packed_stream<xivres::standard_compressing_packer>(entryPathSpec, std::make_shared<xivres::file_stream>(target), Z_BEST_COMPRESSION).read_vector<char>();

							if (m_backgroundWorkerProgressWindow->GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
								return;

							const auto entryLine = std::format("{}\n", nlohmann::json::object({
								{"FullPath", xivres::util::replace(entryPathSpec.text(), std::string("\\"), std::string("/"))},
								{"ModOffset", ttmpdPtr},
								{"ModSize", dv.size()},
								{"DatFile", datFile},
								}).dump());
							ttmplPtr += ttmpl.Write(ttmplPtr, std::span(entryLine));
							ttmpdPtr += ttmpd.Write(ttmpdPtr, std::span(dv));
							index++;
						} catch (const std::exception& e) {
							m_logger->Format<LogLevel::Error>(LogCategory::General, "{}: {}\n", target.wstring(), e.what());
						}
					}
					});

				do {
					m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.FormatStringRes(IDS_TITLE_EXPORTTTMPPROGRESS, pLastStartedTargetFile ? *pLastStartedTargetFile : std::wstring(), index, count));
					if (index == count)
						m_backgroundWorkerProgressWindow->UpdateProgress(0, 0);
					else
						m_backgroundWorkerProgressWindow->UpdateProgress(index, count);
					m_backgroundWorkerProgressWindow->Show();
				} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
				workerThread.Wait();
				m_backgroundWorkerThread = nullptr;
				m_backgroundWorkerProgressWindow = nullptr;
				if (m_ttmpRescanPending.exchange(false))
					PostMessageW(m_hWnd, WM_COMMAND, ID_MODDING_TTMP_REFRESH, 0);
				});
			return;
		}
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Configure(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_CONFIGURE_SETTINGS:
			// The settings are in this window.
			m_config->Runtime.Ui.MainWindow.Show = true;
			SetForegroundWindow(m_hWnd);
			return;

		case ID_CONFIGURE_EDITRUNTIMECONFIGURATION:
			if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
				SetForegroundWindow(m_runtimeConfigEditor->Handle());
			else {
				m_runtimeConfigEditor.reset();
				m_runtimeConfigEditor = std::make_unique<ConfigWindow>(IDS_WINDOW_RUNTIME_CONFIG_EDITOR, &m_config->Runtime);
			}
			return;

		case ID_CONFIGURE_EDITOPCODECONFIGURATION:
			if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
				SetForegroundWindow(m_gameConfigEditor->Handle());
			else {
				m_gameConfigEditor.reset();
				m_gameConfigEditor = std::make_unique<ConfigWindow>(IDS_WINDOW_OPCODE_CONFIG_EDITOR, &m_config->Game);
			}
			return;

		case ID_CONFIGURE_GAMEFIX_OPENDIRECTORY:
			EnsureAndOpenDirectory(m_config->PatchCode.GetDirectory());
			return;

		case ID_CONFIGURE_CHECKFORUPDATEDOPCODES:
			CheckUpdatedOpcodes(true);
			return;

		case ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP:
			config.Opcodes.CheckForUpdatesOnStartup.Toggle();
			return;

		case ID_CONFIGURE_USEMORECPUTIME:
			config.FramerateControl.UseMoreCpuTime.Toggle();
			return;

		case ID_CONFIGURE_BACKGROUND_FRAMERATE_LIMIT:
			config.FramerateControl.UseBackgroundLimit.Toggle();
			return;

		case ID_CONFIGURE_LOCKFRAMERATE:
			m_cleanupFramerateLockDialog = Dialog::FramerateLockingDialog::Show(m_app, m_hWnd);
			return;

		case ID_CONFIGURE_SYNCHRONIZEPROCESSING:
			config.FramerateControl.SynchronizeProcessing.Toggle();
			return;

		case ID_CONFIGURE_WINDOWTITLE_PID_NONE:
			config.GameWindow.TitleMode = GameWindowTitleMode::None;
			return;

		case ID_CONFIGURE_WINDOWTITLE_PID_PREFIX:
			config.GameWindow.TitleMode = GameWindowTitleMode::Prefix;
			return;

		case ID_CONFIGURE_WINDOWTITLE_PID_SUFFIX:
			config.GameWindow.TitleMode = GameWindowTitleMode::Suffix;
			return;

		case ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT:
			config.Ui.Language = Language::SystemDefault;
			return;

		case ID_CONFIGURE_LANGUAGE_ENGLISH:
			config.Ui.Language = Language::English;
			return;

		case ID_CONFIGURE_LANGUAGE_KOREAN:
			config.Ui.Language = Language::Korean;
			return;

		case ID_CONFIGURE_LANGUAGE_JAPANESE:
			config.Ui.Language = Language::Japanese;
			return;

		case ID_CONFIGURE_THEME_SYSTEM:
			config.Ui.ThemeMode = ThemeMode::System;
			return;

		case ID_CONFIGURE_THEME_LIGHT:
			config.Ui.ThemeMode = ThemeMode::Light;
			return;

		case ID_CONFIGURE_THEME_DARK:
			config.Ui.ThemeMode = ThemeMode::Dark;
			return;

		case ID_CONFIGURE_OPENCONFIGURATIONDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath());
			return;

		case ID_CONFIGURE_RELOAD:
			m_config->Reload();
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_View(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_VIEW_ALWAYSONTOP:
			config.Ui.MainWindow.AlwaysOnTop.Toggle();
			return;

		case ID_VIEW_ALWAYSONTOPGAME:
			config.GameWindow.AlwaysOnTop.Toggle();
			return;

		case ID_VIEW_HIDEONMINIMIZE:
			config.Ui.MainWindow.HideOnMinimize.Toggle();
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Help(int menuId) {
	switch (menuId) {
		case ID_HELP_OPENHELPWEBPAGE:
		case ID_HELP_OPENHOMEPAGE: {
			SHELLEXECUTEINFOW shex{
				.cbSize = sizeof shex,
				.hwnd = m_hWnd,
				.lpFile = m_config->Runtime.GetStringRes(menuId == ID_HELP_OPENHELPWEBPAGE ? IDS_URL_HELP : IDS_URL_HOMEPAGE),
				.nShow = SW_SHOW,
			};
			if (!ShellExecuteExW(&shex))
				throw Utils::Win32::Error("ShellExecuteW");
			return;
		}
	}
}

std::vector<std::filesystem::path> XivAlexander::Apps::MainApp::Window::MainWindow::ChooseFileToOpen(std::span<const COMDLG_FILTERSPEC> fileTypes, UINT nTitleResId, const std::filesystem::path& defaultPath) const {
	try {
		IFileOpenDialogPtr pDialog;
		DWORD dwFlags;
		Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(static_cast<UINT>(fileTypes.size()), fileTypes.data()));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(0));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(m_config->Runtime.GetStringRes(nTitleResId)));
		Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR));
		if (!defaultPath.empty()) {
			IShellItemPtr defaultDir;
			if (!FAILED(SHCreateItemFromParsingName(defaultPath.c_str(), nullptr, defaultDir.GetIID(), reinterpret_cast<void**>(&defaultDir))))
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetDefaultFolder(defaultDir));
		}
		Utils::Win32::Error::ThrowIfFailed(pDialog->Show(m_hWnd), true);

		IShellItemArrayPtr items;
		Utils::Win32::Error::ThrowIfFailed(pDialog->GetResults(&items));

		DWORD count = 0;
		Utils::Win32::Error::ThrowIfFailed(items->GetCount(&count));

		std::vector<std::filesystem::path> result;
		for (DWORD i = 0; i < count; ++i) {
			IShellItemPtr item;
			Utils::Win32::Error::ThrowIfFailed(items->GetItemAt(i, &item));

			PWSTR pszFileName;
			Utils::Win32::Error::ThrowIfFailed(item->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
			result.emplace_back(pszFileName);
			CoTaskMemFree(pszFileName);
		}
		return result;

	} catch (const Utils::Win32::CancelledError&) {
		return {};
	}
}

std::string XivAlexander::Apps::MainApp::Window::MainWindow::InstallTTMP(const std::filesystem::path& path, ProgressPopupWindow& progressWindow) {
	const auto targetDirectory = ResolvePrimaryDirectory(m_config->Runtime.Modding.Ttmp.SearchDirectories.Value(), L"TexToolsMods");
	if (path.empty())
		return "";

	std::filesystem::path temporaryTtmpDirectory;
	std::filesystem::path targetTtmpDirectory;
	const auto finalizer = xivres::util::on_dtor([&] {
		if (temporaryTtmpDirectory.empty())
			return;

		try {
			remove_all(temporaryTtmpDirectory);
		} catch (const std::exception& e) {
			Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONWARNING, m_config->Runtime.FormatStringRes(IDS_ERROR_REMOVETEMPORARYDIRECTORY, temporaryTtmpDirectory.wstring(), e.what()));
		}
		});
	temporaryTtmpDirectory = targetDirectory / std::format(L"TEMP_{}", GetTickCount64());
	create_directories(temporaryTtmpDirectory);

	auto offerConfiguration = false;
	{
		char header[2];
		const auto f = Utils::Win32::Handle::FromCreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
		f.Read(0, std::span<char>(header));

		if (header[0] == 'P' && header[1] == 'K') {
			// libzip takes file names in UTF-8.
			const auto pArc = new libzippp::ZipArchive(xivres::util::unicode::convert<std::string>(path.wstring()));
			pArc->open();
			const auto freeArc = xivres::util::on_dtor([pArc] {
				pArc->close();
				delete pArc;
				});

			bool mplFound = false, mpdFound = false;
			for (const auto& entry : pArc->getEntries()) {
				auto name = xivres::util::unicode::convert<std::wstring>(entry.getName());
				while (!name.empty() && (name[0] == L'/' || name[0] == L'\\'))
					name = name.substr(1);
				if (name.empty())
					continue;

				CharLowerW(&name[0]);
				mplFound |= name == L"ttmpl.mpl";
				mpdFound |= name == L"ttmpd.mpd";
			}

			if (!mplFound || !mpdFound)
				throw std::runtime_error(xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_ERROR_ARCHIVE_MISSING_TTMP)));

			for (const auto& entry : pArc->getEntries()) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					return "";

				auto name = xivres::util::unicode::convert<std::wstring>(entry.getName());
				while (!name.empty() && (name[0] == L'/' || name[0] == L'\\'))
					name = name.substr(1);
				if (name.empty())
					continue;

				std::ofstream o(temporaryTtmpDirectory / entry.getName(), std::ios::binary | std::ios::out);
				entry.readContent(o);
			}

			{
				const auto ttmpl = xivres::textools::mod_pack_json::from_stream(xivres::file_stream(temporaryTtmpDirectory / "TTMPL.mpl"));
				if (!ttmpl.ModPackPages.empty())
					offerConfiguration = true;

				const auto name = CreateTtmpDirectoryName(ttmpl.Name, path.parent_path().filename());
				targetTtmpDirectory = targetDirectory / name;
				if (exists(targetTtmpDirectory)) {
					for (int i = 0; exists(targetTtmpDirectory); i++)
						targetTtmpDirectory = targetDirectory / std::format(L"{}_{}", name, i);
				}
			}

		} else {
			const auto ttmpdPath = path.parent_path() / "TTMPD.mpd";
			if (!exists(ttmpdPath))
				throw std::runtime_error(xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_ERROR_TTMPD_MISSING)));
			const auto ttmpl = xivres::textools::mod_pack_json::from_stream(xivres::file_stream(path));
			if (!ttmpl.ModPackPages.empty())
				offerConfiguration = true;
			const auto possibleChoicesPath = path.parent_path() / "choices.json";

			const auto name = CreateTtmpDirectoryName(ttmpl.Name, path.parent_path().filename());
			targetTtmpDirectory = targetDirectory / name;
			if (exists(targetTtmpDirectory)) {
				for (int i = 0; exists(targetTtmpDirectory); i++)
					targetTtmpDirectory = targetDirectory / std::format(L"{}_{}", name, i);
			}

			if (!CopyFileW(path.c_str(), (temporaryTtmpDirectory / "TTMPL.mpl").c_str(), TRUE))
				throw Utils::Win32::Error("CopyFileW");
			if (!CopyFileW(ttmpdPath.c_str(), (temporaryTtmpDirectory / "TTMPD.mpd").c_str(), TRUE))
				throw Utils::Win32::Error("CopyFileW");
			if (exists(possibleChoicesPath) && !CopyFileW(possibleChoicesPath.c_str(), (temporaryTtmpDirectory / "choices.json").c_str(), TRUE))
				throw Utils::Win32::Error("CopyFileW");
		}
	}

	std::filesystem::rename(temporaryTtmpDirectory, targetTtmpDirectory);

	if (auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks())
		sqpacks->AddNewTtmp(targetTtmpDirectory / "TTMPL.mpl", true, progressWindow);

	return offerConfiguration ? xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_NOTIFY_TTMP_HAS_CONFIGURATION)) : "";
}

std::pair<std::filesystem::path, std::string> XivAlexander::Apps::MainApp::Window::MainWindow::InstallAnyFile(const std::filesystem::path& path, ProgressPopupWindow& progressWindow) {
	auto fileNameLower = path.filename().wstring();
	CharLowerW(fileNameLower.data());
	if (fileNameLower == L"ttmpd.mpd" || fileNameLower == L"choices.json")
		return { path, {} };
	if (!fileNameLower.ends_with(L".json")
		&& !fileNameLower.ends_with(L".mpl")
		&& !fileNameLower.ends_with(L".ttmp")
		&& !fileNameLower.ends_with(L".ttmp2"))
		return { path, {} };

	const auto file = Utils::Win32::Handle::FromCreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	char buf[2];
	file.Read(0, buf, 2);
	if (buf[0] == 'P' && buf[1] == 'K') {
		const auto msg = InstallTTMP(path, progressWindow);
		return std::make_pair(path, msg.empty() ? xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_RESULT_TTMP_INSTALLED)) : msg);
	}
	const auto fileSize = file.GetFileSize();
	if (fileSize > 1048576)
		throw std::runtime_error("File too big");

	if (const auto ttmpl = xivres::textools::mod_pack_json::from_stream(xivres::file_stream(path)); !ttmpl.SimpleModsList.empty() || !ttmpl.ModPackPages.empty()) {
		const auto msg = InstallTTMP(path, progressWindow);
		return std::make_pair(path, msg.empty() ? xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_RESULT_TTMP_INSTALLED)) : msg);
	}

	throw std::runtime_error(xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE)));
}

void XivAlexander::Apps::MainApp::Window::MainWindow::InstallMultipleFiles(const std::vector<std::filesystem::path>& paths) {
	if (paths.empty())
		return;

	std::vector<std::pair<std::filesystem::path, std::string>> success;
	std::vector<std::pair<std::filesystem::path, std::string>> ignored;
	{
		ProgressPopupWindow progressWindow(m_hWnd);
		progressWindow.UpdateMessage(xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_TITLE_IMPORTING)));
		progressWindow.UpdateProgress(0, 0);
		const auto showAfter = GetTickCount64() + 500;

		const auto adderThread = Utils::Win32::Thread(L"DropFiles Handler", [&] {
			for (const auto& path : paths) {
				try {
					if (is_directory(path)) {
						auto anyAdded = false;
						for (const auto& item : std::filesystem::recursive_directory_iterator(path)) {
							if (!item.is_directory()) {
								try {
									auto res = InstallAnyFile(item.path(), progressWindow);
									if (!res.second.empty()) {
										success.emplace_back(std::move(res));
										anyAdded = true;
									}
								} catch (const std::exception& e) {
									ignored.emplace_back(item.path(), e.what());
									anyAdded = true;
								}
							}
						}
						if (!anyAdded)
							ignored.emplace_back(path, xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_ERROR_NO_MATCHING_FILES)));
					} else {
						try {
							auto res = InstallAnyFile(path, progressWindow);
							if (res.second.empty())
								ignored.emplace_back(path, xivres::util::unicode::convert<std::string>(m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE)));
							else
								success.emplace_back(std::move(res));
						} catch (const std::exception& e) {
							ignored.emplace_back(path, e.what());
						}
					}
				} catch (const std::exception& e) {
					ignored.emplace_back(path, e.what());
				}
			}
			});

		while (WAIT_TIMEOUT == progressWindow.DoModalLoop(100, { adderThread })) {
			if (showAfter < GetTickCount64())
				progressWindow.Show();
		}
		adderThread.Wait();
	}

	std::string report;
	for (const auto& pair : success) {
		if (!report.empty())
			report += "\n";
		report += std::format("OK: {}: {}", pair.first.wstring(), pair.second);
	}
	if (!report.empty())
		report += "\n";
	for (const auto& pair : ignored) {
		if (!report.empty())
			report += "\n";
		report += std::format("Error: {}: {}", pair.first.wstring(), pair.second);
	}

	if (!report.empty())
		Dll::MessageBoxF(m_hWnd, MB_OK, xivres::util::unicode::convert<std::wstring>(report));
	else
		Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.GetStringRes(IDS_APP_NAME), L"{}\n\n{}", m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE), xivres::util::unicode::convert<std::wstring>(report));

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

void XivAlexander::Apps::MainApp::Window::MainWindow::CheckUpdatedOpcodes(bool showResultMessageBox) {
	void(Utils::Win32::Thread(L"CheckUpdatedOpcodes", [this, showResultMessageBox] {
		const auto releaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();

		auto opcodes = RemoteConfigUpdateResult::NotFound;
		size_t patchCodesChanged = 0;
		std::string error;

		try {
			opcodes = UpdateConfigFromRemote(m_config->Game, std::format("https://raw.githubusercontent.com/Soreepeong/XivAlexander/main/StaticData/OpcodeDefinition/game.{}.{}.json", releaseInfo.CountryCode, releaseInfo.GameVersion));
			switch (opcodes) {
				case RemoteConfigUpdateResult::NotFound:
					m_logger->Log(LogCategory::General, "No updates to opcodes yet.");
					break;
				case RemoteConfigUpdateResult::NotChanged:
					m_logger->Log(LogCategory::General, "No updates to opcodes.");
					break;
				case RemoteConfigUpdateResult::Changed:
					m_logger->Log(LogCategory::General, "Opcodes updated.");
					break;
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Error>(LogCategory::General, "Opcode update check failed: {}", e.what());
			error = e.what();
		}

		try {
			patchCodesChanged = UpdatePatchCodesFromRemote(m_config->PatchCode.GetDirectory());
			if (patchCodesChanged) {
				m_config->PatchCode.Reload();
				m_logger->Format(LogCategory::General, "Patch codes updated ({} files).", patchCodesChanged);
			} else {
				m_logger->Log(LogCategory::General, "No updates to patch codes.");
			}
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Error>(LogCategory::General, "Patch code update check failed: {}", e.what());
			if (error.empty())
				error = e.what();
		}

		if (!showResultMessageBox)
			return;

		if (!error.empty())
			Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, error);
		else if (opcodes == RemoteConfigUpdateResult::Changed || patchCodesChanged)
			Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_OK_CHANGED));
		else if (opcodes == RemoteConfigUpdateResult::NotFound)
			Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_ERROR_404));
		else
			Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_OK_NOTCHANGED));
	}));
}

void XivAlexander::Apps::MainApp::Window::MainWindow::BatchTtmpOperation(Features::Modding::NestedTtmp& parent, int menuId) {
	try {
		std::wstring msg;
		switch (menuId) {
			case ID_MODDING_TTMP_REMOVEALL:
				msg = m_config->Runtime.FormatStringRes(IDS_CONFIRM_REMOVEALLTTMP, parent.Path.wstring());
				break;

			case ID_MODDING_TTMP_ENABLEALL:
				msg = m_config->Runtime.FormatStringRes(IDS_CONFIRM_ENABLEALLTTMP, parent.Path.wstring());
				break;

			case ID_MODDING_TTMP_DISABLEALL:
				msg = m_config->Runtime.FormatStringRes(IDS_CONFIRM_DISABLEALLTTMP, parent.Path.wstring());
				break;
		}
		if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, msg) == IDYES) {
			if (auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks()) {
				m_backgroundWorkerThread = Utils::Win32::Thread(L"BatchTtmpOperationOnOtherThread", [this, &parent, &sqpacks, menuId] {
					m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);
					const auto workerThread = Utils::Win32::Thread(L"BatchTtmpOperation", [&] {
						auto lock = sqpacks->LockTtmps();
						parent.Traverse(false, [&](Features::Modding::NestedTtmp& nestedTtmp) {
							if (!nestedTtmp.Ttmp)
								return;

							const auto& set = *nestedTtmp.Ttmp;
							switch (menuId) {
								case ID_MODDING_TTMP_REMOVEALL:
									sqpacks->DeleteTtmp(set.ListPath, false);
									break;

								case ID_MODDING_TTMP_ENABLEALL:
									nestedTtmp.Enabled = true;
									sqpacks->ApplyTtmpChanges(nestedTtmp, false);
									break;

								case ID_MODDING_TTMP_DISABLEALL:
									nestedTtmp.Enabled = false;
									sqpacks->ApplyTtmpChanges(nestedTtmp, false);
									break;
							}
							});
						lock.unlock();

						sqpacks->RescanTtmp(*m_backgroundWorkerProgressWindow);
						});

					do {
						m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
						m_backgroundWorkerProgressWindow->Show();
					} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
					workerThread.Wait();
					m_backgroundWorkerThread = nullptr;
					m_backgroundWorkerProgressWindow = nullptr;
					if (m_ttmpRescanPending.exchange(false))
						PostMessageW(m_hWnd, WM_COMMAND, ID_MODDING_TTMP_REFRESH, 0);
					});
			}
		}
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}
