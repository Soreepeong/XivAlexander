﻿#include "pch.h"
#include "Apps/MainApp/Window/MainWindow.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Sqex/CommandLine.h>
#include <XivAlexanderCommon/Sqex/FontCsv/CreateConfig.h>
#include <XivAlexanderCommon/Sqex/Sound/MusicImporter.h>
#include <XivAlexanderCommon/Sqex/Sqpack/BinaryEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/EmptyOrObfuscatedEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/ModelEntryProvider.h>
#include <XivAlexanderCommon/Sqex/Sqpack/TextureEntryProvider.h>
#include <XivAlexanderCommon/Sqex/ThirdParty/TexTools.h>
#include <XivAlexanderCommon/Utils/Win32/Resource.h>
#include <XivAlexanderCommon/Utils/Win32/TaskDialogBuilder.h>
#include <XivAlexanderCommon/Utils/Win32/ThreadPool.h>

#include "Apps/MainApp/App.h"
#include "Apps/MainApp/Internal/GameResourceOverrider.h"
#include "Apps/MainApp/Internal/MainThreadTimingHandler.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Apps/MainApp/Internal/VirtualSqPacks.h"
#include "Apps/MainApp/Window/ConfigWindow.h"
#include "Apps/MainApp/Window/Dialog/FramerateLockingDialog.h"
#include "Apps/MainApp/Window/ProgressPopupWindow.h"
#include "Misc/ExcelTransformConfig.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Logger.h"
#include "resource.h"
#include "XivAlexander.h"

#define WM_COPYGLOBALDATA 0x0049

enum : UINT {
	WmApp = WM_APP + 1,
	WmTrayCallback,
	WmRepopulateMenu,
};

static const int TrayItemId = 0x4c19fd7a;
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
	wcex.lpszClassName = L"XivAlexander::Window::MainWindow";
	wcex.hIconSm = hIcon;
	return wcex;
}

XivAlexander::Apps::MainApp::Window::MainWindow::MainWindow(Apps::MainApp::App& app, std::function<void()> unloadFunction)
	: BaseWindow(WindowClass(), nullptr, WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, 480, 160, nullptr, nullptr)
	, m_app(app)
	, m_triggerUnload(std::move(unloadFunction))
	, m_uTaskbarRestartMessage(RegisterWindowMessageW(L"TaskbarCreated"))
	, m_path(Utils::Win32::Process::Current().PathOf())
	, m_bUseElevation(Utils::Win32::IsUserAnAdmin())
	, m_launchParameters([this]() -> decltype(m_launchParameters) {
	try {
		auto res = Sqex::CommandLine::FromString(Dll::GetOriginalCommandLine(), &m_bUseParameterObfuscation);
		if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
			Sqex::CommandLine::WellKnown::SetLanguage(res, m_config->Runtime.RememberedGameLaunchLanguage);
		if (m_config->Runtime.RememberedGameLaunchRegion != Sqex::Region::Unspecified)
			Sqex::CommandLine::WellKnown::SetRegion(res, m_config->Runtime.RememberedGameLaunchRegion);
		return res;
	} catch (const std::exception& e) {
		m_logger->Format<LogLevel::Warning>(LogCategory::General, m_config->Runtime.GetLangId(), IDS_WARNING_GAME_PARAMETER_PARSE, e.what());
		return {};
	}
		}())
	, m_startupArgumentsForDisplay([this]() {
			auto params{ m_launchParameters };
			for (auto& [k, v] : params) {
				if (k == "DEV.TestSID") {
					for (auto& c : v)
						c = '*';
					v += std::format("({})", v.size());
				}
			}
			return Sqex::CommandLine::ToString(params, false);
		}()) {

	try {
		m_gameReleaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();

		if (m_gameReleaseInfo.Region == Sqex::GameReleaseRegion::International && !m_launchParameters.empty()) {
			m_gameLanguage = Sqex::CommandLine::WellKnown::GetLanguage(m_launchParameters, Sqex::Language::English);
			m_gameRegion = Sqex::CommandLine::WellKnown::GetRegion(m_launchParameters, Sqex::Region::NorthAmerica);
		} else if (m_gameReleaseInfo.Region == Sqex::GameReleaseRegion::Chinese) {
			m_gameLanguage = Sqex::Language::ChineseSimplified;
			m_gameRegion = Sqex::Region::China;
		} else if (m_gameReleaseInfo.Region == Sqex::GameReleaseRegion::Korean) {
			m_gameLanguage = Sqex::Language::Korean;
			m_gameRegion = Sqex::Region::Korea;
		}
	} catch (...) {
	}

	RegisterTrayIcon();

	// Try to restore tray icon every 5 seconds in case things go wrong
	SetTimer(m_hWnd, TimerIdReregisterTrayIcon, 5000, nullptr);

	SetTimer(m_hWnd, TimerIdRepaint, 1000, nullptr);

	m_cleanup += m_config->Runtime.AlwaysOnTop_XivAlexMainWindow.OnChange([this]() {
		SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexMainWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		});

	m_cleanup += m_config->Runtime.ShowControlWindow.OnChange([this]() {
		ShowWindow(m_hWnd, m_config->Runtime.ShowControlWindow ? SW_SHOWNORMAL : SW_HIDE);
		if (m_config->Runtime.ShowControlWindow)
			SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexMainWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		});
	if (m_config->Runtime.ShowControlWindow) {
		ShowWindow(m_hWnd, SW_SHOW);
		SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexMainWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	m_cleanup += m_config->Runtime.AdditionalSqpackRootDirectories.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.ExcelTransformConfigFiles.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.OverrideFontConfig.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.MusicImportConfig.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.MusicImportConfig_Directories.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.TtmpShowDedicatedMenu.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.TtmpUseSubdirectoryTogglingOnFlattenedView.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.TtmpFlattenSubdirectoryDisplay.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Runtime.EnabledPatchCodes.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});
	m_cleanup += m_config->Game.PatchCode.OnChange([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});

	if (!m_sqpacksLoaded) {
		if (auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks()) {
			m_cleanup += sqpacks->OnTtmpSetsChanged([this]() { RepopulateMenu(); });
			m_sqpacksLoaded = true;
		}
	}

	m_cleanup += m_app.GetSocketHook().OnSocketFound([this](auto&) {
		InvalidateRect(m_hWnd, nullptr, false);
		});
	m_cleanup += m_app.GetSocketHook().OnSocketGone([this](auto&) {
		InvalidateRect(m_hWnd, nullptr, false);
		});
	ApplyLanguage(m_config->Runtime.GetLangId());

	m_cleanup += m_app.GetGameResourceOverrider().OnVirtualSqPacksInitialized([this]() {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		});

	DragAcceptFiles(m_hWnd, TRUE);
	ChangeWindowMessageFilterEx(m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(m_hWnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);

	if (m_config->Runtime.CheckForUpdatedOpcodesOnStartup)
		CheckUpdatedOpcodes(false);
}

XivAlexander::Apps::MainApp::Window::MainWindow::~MainWindow() {
	m_cleanupFramerateLockDialog.Clear();
	m_cleanup.Clear();
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
	InvalidateRect(m_hWnd, nullptr, FALSE);
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
					m_config->Runtime.ShowControlWindow = false;
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
			buf.resize(DragQueryFileW(hDrop, i, &buf[0], static_cast<UINT>(buf.size())));
			paths.emplace_back(std::move(buf));
		}

		InstallMultipleFiles(paths);

	} else if (uMsg == WM_COPYDATA) {
		const auto cds = *reinterpret_cast<const COPYDATASTRUCT*>(lParam);
		if (IsBadReadPtr(&cds, sizeof cds))
			return 0;

		if (cds.cbData && IsBadReadPtr(cds.lpData, cds.cbData))
			return 0;

		// continue execution; no return here

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
		const auto iconId = HIWORD(lParam);
		const auto eventId = LOWORD(lParam);
		if (eventId == WM_CONTEXTMENU) {
			ShowContextMenu();
		} else if (eventId == WM_LBUTTONUP) {
			const auto now = GetTickCount64();
			auto willShowControlWindow = false;
			if (m_lastTrayIconLeftButtonUp + GetDoubleClickTime() > now) {
				if (m_config->Runtime.ShowControlWindow.Toggle())
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
		std::wstring str;
		try {
			const auto window = Utils::QpcUs() - 1000000;
			uint64_t msgPumpMean{}, msgPumpDev{};
			double msgPumpCount{};
			if (auto& handler = m_app.GetMainThreadTimingHelper()) {
				std::tie(msgPumpMean, msgPumpDev) = handler->GetMessagePumpIntervalTrackerUs().MeanAndDeviation(window);
				msgPumpCount = handler->GetMessagePumpIntervalTrackerUs().CountFractional(window);
			}
			str = m_config->Runtime.FormatStringRes(IDS_MAIN_TEXT,
				GetCurrentProcessId(),
				m_path, 
				m_startupArgumentsForDisplay,
				m_gameReleaseInfo.GameVersion, m_gameReleaseInfo.CountryCode,
				msgPumpMean, msgPumpDev, msgPumpCount,
				m_app.GetSocketHook().Describe());
		} catch (...) {
			// pass
		}
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
	} else if (uMsg == WM_SIZE) {
		if (wParam == SIZE_MINIMIZED && m_config->Runtime.HideOnMinimize_XivAlexMainWindow) {
			m_config->Runtime.ShowControlWindow = false;
			return 0;
		}
	}
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnDestroy() {
	m_triggerUnload();

	if (const auto replaceMusicsProgressWindow = decltype(m_backgroundWorkerProgressWindow)(m_backgroundWorkerProgressWindow))
		replaceMusicsProgressWindow->Cancel();
	if (const auto replaceMusicsThread = decltype(m_backgroundWorkerThread)(m_backgroundWorkerThread))
		replaceMusicsThread.Wait();

	if (m_runtimeConfigEditor)
		delete m_runtimeConfigEditor;
	m_runtimeConfigEditor = nullptr;
	if (m_gameConfigEditor)
		delete m_gameConfigEditor;
	m_gameConfigEditor = nullptr;
	RemoveTrayIcon();
	BaseWindow::OnDestroy();
	PostQuitMessage(0);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu() {
	if (GetWindowThreadProcessId(m_hWnd, nullptr) != GetCurrentThreadId()) {
		PostMessageW(m_hWnd, WmRepopulateMenu, 0, 0);
		return;
	}

	auto menu = Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_TRAY_MENU), m_config->Runtime.GetLangId());

	const auto title = std::format(L"{}: {}, {}, {}",
		m_config->Runtime.GetStringRes(IDS_APP_NAME), GetCurrentProcessId(), m_gameReleaseInfo.CountryCode, m_gameReleaseInfo.GameVersion);
	ModifyMenuW(menu, ID_FILE_CURRENTINFO, MF_BYCOMMAND | MF_DISABLED, ID_FILE_CURRENTINFO, title.c_str());

	m_menuIdCallbacks.clear();
	{
		RepopulateMenu_GameFix(GetSubMenu(GetSubMenu(menu, 5), 5));

		// note: ttmp top-level menu gets deleted so top menu index changes

		const auto hModMenu = GetSubMenu(menu, 3);
		const auto hOuterTtmpMenu = GetSubMenu(menu, 4);

		int index = 0;
		for (const auto menuItemCount = GetMenuItemCount(hModMenu); index < menuItemCount; index++) {
			if (GetMenuItemID(hModMenu, index) == ID_MODDING_SUBMENUMARKER)
				break;
		}
		DeleteMenu(hModMenu, index, MF_BYPOSITION);
		const auto hFontConfigMenu = GetSubMenu(hModMenu, index++);
		const auto hAdditionalSqpackRootDirectoriesMenu = GetSubMenu(hModMenu, index++);
		const auto hExdfTransformationRulesMenu = GetSubMenu(hModMenu, index++);
		const auto hUpgradeMusicQualityMenu = GetSubMenu(hModMenu, index++);
		const auto hInnerTtmpMenu = GetSubMenu(hModMenu, index++);

		if (m_config->Runtime.TtmpShowDedicatedMenu) {
			while (GetMenuItemCount(hOuterTtmpMenu))
				DeleteMenu(hOuterTtmpMenu, 0, MF_BYPOSITION);
			for (index = 0; index < GetMenuItemCount(hInnerTtmpMenu); ++index) {
				MENUITEMINFOW mii = {
					.cbSize = sizeof mii,
					.fMask = MIIM_TYPE,
				};
				GetMenuItemInfoW(hInnerTtmpMenu, index, TRUE, &mii);
				if (mii.fType & MFT_SEPARATOR) {
					DeleteMenu(hInnerTtmpMenu, index, MF_BYPOSITION);
					break;
				}
			}
		} else {
			DeleteMenu(menu, 4, MF_BYPOSITION);
		}

		RepopulateMenu_FontConfig(hFontConfigMenu);
		RepopulateMenu_AdditionalSqpackRootDirectories(hAdditionalSqpackRootDirectoriesMenu);
		RepopulateMenu_ExdfTransformationRules(hExdfTransformationRulesMenu);
		RepopulateMenu_UpgradeMusicQuality(hUpgradeMusicQualityMenu);
		RepopulateMenu_Ttmp(hInnerTtmpMenu, hOuterTtmpMenu);
	}

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
	GetMenuStringW(hParentMenu, commandId, &res[0], static_cast<int>(res.size()), MF_BYCOMMAND);
	return res;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_FontConfig(HMENU hParentMenu) {
	const auto currentConfig = m_config->TranslatePath(m_config->Runtime.OverrideFontConfig.Value());

	auto count = 0;
	bool foundEq = false;
	try {
		for (const auto& entry : std::filesystem::directory_iterator(m_config->Init.ResolveConfigStorageDirectoryPath() / "FontConfig")) {
			auto lower = entry.path().wstring();
			CharLowerW(&lower[0]);
			if (!lower.ends_with(L".json") || entry.is_directory())
				continue;
			auto eq = false;
			try {
				eq = equivalent(entry.path(), currentConfig);
				foundEq |= eq;
			} catch (...) {
				// pass
			}
			AppendMenuW(hParentMenu, MF_STRING | (eq ? MF_CHECKED : 0), RepopulateMenu_AllocateMenuId([this, path = entry.path()]() {
				m_config->Runtime.OverrideFontConfig = path;
			}), entry.path().filename().wstring().c_str());
			count++;
		}
	} catch (const std::filesystem::filesystem_error&) {
		// pass
	}
	if (!foundEq && !currentConfig.empty()) {
		AppendMenuW(hParentMenu, MF_STRING | MF_CHECKED, RepopulateMenu_AllocateMenuId([]() { /* do nothing */ }), currentConfig.wstring().c_str());
		count++;
	}
	if (count)
		DeleteMenu(hParentMenu, ID_MODDING_CHANGEFONT_NOENTRY, MF_BYCOMMAND);
	DeleteMenu(hParentMenu, ID_MODDING_CHANGEFONT_ENTRY, MF_BYCOMMAND);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_AdditionalSqpackRootDirectories(HMENU hParentMenu) {
	auto count = 0;
	std::set existing{ m_gameReleaseInfo.GamePath() };
	for (const auto& additionalRoot : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
		existing.insert(additionalRoot);
		AppendMenuW(hParentMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, additionalRoot]() {
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO, m_config->Runtime.FormatStringRes(IDS_CONFIRM_REMOVE_ADDITIONAL_ROOT, additionalRoot.wstring())) == IDNO)
				return;

			std::vector<std::filesystem::path> newValue;
			for (const auto& path : m_config->Runtime.AdditionalSqpackRootDirectories.Value()) {
				if (path != additionalRoot)
					newValue.emplace_back(path);
			}
			m_config->Runtime.AdditionalSqpackRootDirectories = std::move(newValue);
			}), additionalRoot.wstring().c_str());
		count++;
	}
	if (count)
		DeleteMenu(hParentMenu, ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_NOENTRY, MF_BYCOMMAND);
	DeleteMenu(hParentMenu, ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_ENTRY, MF_BYCOMMAND);

	const auto hAddMenu = GetSubMenu(hParentMenu, 0);
	count = 0;
	for (const auto& info : Misc::GameInstallationDetector::FindInstallations()) {
		if (existing.contains(info.GamePath()))
			continue;
		AppendMenuW(hAddMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, info]() {
			AddAdditionalGameRootDirectory(info.GamePath());
			}), std::format(L"{} ({}, {})", info.GamePath().wstring(), info.CountryCode, info.GameVersion).c_str());
		count++;
	}
	if (count)
		DeleteMenu(hAddMenu, ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_ADD_EMPTY, MF_BYCOMMAND);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_ExdfTransformationRules(HMENU hParentMenu) {
	auto count = 0;
	for (const auto& file : m_config->Runtime.ExcelTransformConfigFiles.Value()) {
		AppendMenuW(hParentMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, file]() {
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO, m_config->Runtime.FormatStringRes(IDS_CONFIRM_REMOVE_EXCEL_TRANSFORM_CONFIG_FILE, file.wstring())) == IDNO)
				return;

			std::vector<std::filesystem::path> newValue;
			for (const auto& path : m_config->Runtime.ExcelTransformConfigFiles.Value()) {
				if (path != file)
					newValue.emplace_back(path);
			}
			m_config->Runtime.ExcelTransformConfigFiles = std::move(newValue);
			}), file.wstring().c_str());
		count++;
	}
	if (count)
		DeleteMenu(hParentMenu, ID_MODDING_EXDFTRANSFORMATIONRULES_NOENTRY, MF_BYCOMMAND);
	DeleteMenu(hParentMenu, ID_MODDING_EXDFTRANSFORMATIONRULES_ENTRY, MF_BYCOMMAND);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_UpgradeMusicQuality(HMENU hParentMenu) {
	const auto hTemplateEntryMenu = GetSubMenu(hParentMenu, 0);
	RemoveMenu(hParentMenu, 0, MF_BYPOSITION);
	const auto deleteTemplateMenu = Utils::CallOnDestruction([hTemplateEntryMenu]() { DestroyMenu(hTemplateEntryMenu); });

	int index = 0;
	for (const auto menuItemCount = GetMenuItemCount(hParentMenu); index < menuItemCount; index++) {
		if (GetMenuItemID(hParentMenu, index) == ID_MODDING_REPLACEMUSICS_NORULESCONFIGURED)
			break;
	}
	for (const auto& file : m_config->Runtime.MusicImportConfig.Value()) {
		InsertMenu(hParentMenu, index++, MF_STRING | MF_BYPOSITION, RepopulateMenu_AllocateMenuId([this, file] {
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO, m_config->Runtime.FormatStringRes(IDS_CONFIRM_REMOVE_MUSICIMPORTCONFIG, file.wstring())) == IDNO)
				return;

			std::vector<std::filesystem::path> newValue;
			for (const auto& path : m_config->Runtime.MusicImportConfig.Value()) {
				if (path != file)
					newValue.emplace_back(path);
			}
			m_config->Runtime.MusicImportConfig = std::move(newValue);
			}), file.wstring().c_str());
	}

	index = 0;
	for (const auto menuItemCount = GetMenuItemCount(hParentMenu); index < menuItemCount; index++) {
		if (GetMenuItemID(hParentMenu, index) == ID_MODDING_REPLACEMUSICS_NOSOUCEDIRECTORIESCONFIGURED)
			break;
	}
	for (const auto& [name, paths] : m_config->Runtime.MusicImportConfig_Directories.Value()) {
		const auto hSubMenu = CreatePopupMenu();
		AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, name, paths = paths]() mutable {
			try {
				IFileOpenDialogPtr pDialog;
				DWORD dwFlags;
				Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(m_config->Runtime.FormatStringRes(IDS_TITLE_ADD_MUSICDIRECTORY, name).c_str()));
				Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS));
				Utils::Win32::Error::ThrowIfFailed(pDialog->Show(m_hWnd), true);

				IShellItemPtr pResult;
				PWSTR pszFileName;
				Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
				Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
				if (!pszFileName)
					throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
				const auto freeFileName = Utils::CallOnDestruction([pszFileName]() { CoTaskMemFree(pszFileName); });

				paths.emplace_back(pszFileName);

				auto newValue = m_config->Runtime.MusicImportConfig_Directories.Value();
				newValue.insert_or_assign(name, paths);
				m_config->Runtime.MusicImportConfig_Directories = newValue;

			} catch (const Utils::Win32::CancelledError&) {
				// pass

			} catch (const std::exception& e) {
				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			}
			}), RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_REPLACEMUSICS_SOURCEDIRECTORY_ADD).c_str());

		AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, name]() {
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO, m_config->Runtime.FormatStringRes(IDS_CONFIRM_UNREGISTERALLMUSICSOURCEDIRECTORIES, name)) == IDNO)
				return;

			auto newValue = m_config->Runtime.MusicImportConfig_Directories.Value();
			newValue.insert_or_assign(name, std::vector<std::filesystem::path>());
			m_config->Runtime.MusicImportConfig_Directories = newValue;
			}), RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_REPLACEMUSICS_SOURCEDIRECTORY_CLEAR).c_str());

		AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);

		if (const auto purchaseWebsites = m_config->Runtime.GetMusicDirectoryPurchaseWebsites(name); !purchaseWebsites.empty()) {
			for (const auto& [websiteName, websiteUrl] : purchaseWebsites) {
				AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, websiteUrl]() mutable {
					try {
						Utils::Win32::ShellExecutePathOrThrow(websiteUrl, m_hWnd);
					} catch (const std::exception& e) {
						Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
					}
					}), std::vformat(RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_REPLACEMUSICS_SOURCEDIRECTORY_BUYFROM), std::make_wformat_args(websiteName)).c_str());
			}
			AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);
		}

		for (const auto& path : paths) {
			AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, name, path, paths = paths]() mutable {
				if (Dll::MessageBoxF(m_hWnd, MB_YESNO, m_config->Runtime.FormatStringRes(IDS_CONFIRM_REMOVE_MUSICSOURCEDIRECTORY, name, path)) == IDNO)
					return;

				paths.erase(std::remove_if(paths.begin(), paths.end(), [&path](const auto& r) { return path == r; }), paths.end());

				auto newValue = m_config->Runtime.MusicImportConfig_Directories.Value();
				newValue.insert_or_assign(name, paths);
				m_config->Runtime.MusicImportConfig_Directories = newValue;
				}), path.c_str());
		}
		if (paths.empty())
			AppendMenuW(hSubMenu, MF_STRING | MF_DISABLED, 0, RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_REPLACEMUSICS_SOURCEDIRECTORY_EMPTY).c_str());

		InsertMenuW(hParentMenu, index++, MF_STRING | MF_POPUP | MF_BYPOSITION, reinterpret_cast<UINT_PTR>(hSubMenu), Utils::FromUtf8(name).c_str());
	}

	if (!m_config->Runtime.MusicImportConfig.Value().empty())
		DeleteMenu(hParentMenu, ID_MODDING_REPLACEMUSICS_NORULESCONFIGURED, MF_BYCOMMAND);
	if (!m_config->Runtime.MusicImportConfig_Directories.Value().empty())
		DeleteMenu(hParentMenu, ID_MODDING_REPLACEMUSICS_NOSOUCEDIRECTORIESCONFIGURED, MF_BYCOMMAND);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_Ttmp(HMENU hInnerTtmpMenu, HMENU hOuterTtmpMenu) {
	const auto hTemplateEntryMenu = GetSubMenu(hInnerTtmpMenu, 0);
	RemoveMenu(hInnerTtmpMenu, 0, MF_BYPOSITION);
	const auto deleteTemplateMenu = Utils::CallOnDestruction([hTemplateEntryMenu]() { DestroyMenu(hTemplateEntryMenu); });

	auto count = 0;
	auto ready = false;

	if (auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks()) {
		ready = true;
		if (!m_sqpacksLoaded) {
			m_cleanup += sqpacks->OnTtmpSetsChanged([this]() { RepopulateMenu(); });
			m_sqpacksLoaded = true;
		}

		struct MenuStack {
			HMENU Menu;
			Internal::VirtualSqPacks::NestedTtmp* Item;
			int InsertionIndex;
			bool HideInner;
		};
		std::vector<MenuStack> menuStack;
		sqpacks->GetTtmps()->Traverse(false, [&](Internal::VirtualSqPacks::NestedTtmp& nestedTtmp) {
			if (!nestedTtmp.Parent) {
				if (m_config->Runtime.TtmpShowDedicatedMenu) {
					menuStack.emplace_back(MenuStack{ hOuterTtmpMenu, &nestedTtmp, 0, false });
				} else {
					menuStack.emplace_back(MenuStack{ hInnerTtmpMenu, &nestedTtmp, 0, false });
				}
				return;
			}

			while (menuStack.size() > 1 && nestedTtmp.Parent.get() != menuStack.back().Item)
				menuStack.pop_back();

			if (nestedTtmp.IsGroup()) {
				if (m_config->Runtime.TtmpFlattenSubdirectoryDisplay) {
					std::wstring menuName;
					menuName.resize(3 * (menuStack.size() - 1), L' ');
					menuName += nestedTtmp.Path.filename().wstring();

					const auto skipMenu = menuStack.back().HideInner;

					menuStack.emplace_back(MenuStack{ nullptr, &nestedTtmp, 0, menuStack.back().HideInner || !nestedTtmp.Enabled });

					if (!skipMenu)
						InsertMenuW(menuStack.front().Menu,
							menuStack.front().InsertionIndex++,
							MF_BYPOSITION | MF_STRING | (nestedTtmp.Enabled ? MF_CHECKED : 0) | (m_config->Runtime.TtmpUseSubdirectoryTogglingOnFlattenedView ? 0 : MF_DISABLED),
							RepopulateMenu_AllocateMenuId([this, &nestedTtmp, &sqpacks]() {
								try {
									nestedTtmp.Enabled = !nestedTtmp.Enabled;
									sqpacks->ApplyTtmpChanges(nestedTtmp);
								} catch (const std::exception& e) {
									Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
								}
								}),
							menuName.c_str());
				} else {
					const auto hSubMenu = CreatePopupMenu();
					AppendMenuW(hSubMenu, MF_STRING | (nestedTtmp.Enabled ? MF_CHECKED : 0), RepopulateMenu_AllocateMenuId([this, &nestedTtmp, &sqpacks]() {
						try {
							nestedTtmp.Enabled = !nestedTtmp.Enabled;
							sqpacks->ApplyTtmpChanges(nestedTtmp);
						} catch (const std::exception& e) {
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						}), RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_TTMP_ENTRY_ENABLE).c_str());

					AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);
					AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);

					AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, &nestedTtmp]() {
						BatchTtmpOperation(nestedTtmp, ID_MODDING_TTMP_ENABLEALL);
						}), RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_ENABLEALL).c_str());
					AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, &nestedTtmp]() {
						BatchTtmpOperation(nestedTtmp, ID_MODDING_TTMP_DISABLEALL);
						}), RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_DISABLEALL).c_str());
					AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, &nestedTtmp]() {
						BatchTtmpOperation(nestedTtmp, ID_MODDING_TTMP_REMOVEALL);
						}), RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_REMOVEALL).c_str());

					InsertMenuW(menuStack.back().Menu,
						menuStack.back().InsertionIndex++,
						MF_BYPOSITION | MF_STRING | MF_POPUP | (nestedTtmp.Enabled ? MF_CHECKED : 0),
						reinterpret_cast<UINT_PTR>(hSubMenu),
						nestedTtmp.Path.filename().wstring().c_str());
					menuStack.emplace_back(MenuStack{ hSubMenu, &nestedTtmp, 2 });
				}
				return;
			}

			count++;
			if (menuStack.back().HideInner)
				return;

			auto& ttmpSet = *nestedTtmp.Ttmp;

			const auto hSubMenu = CreatePopupMenu();
			AppendMenuW(hSubMenu, MF_STRING | (nestedTtmp.Enabled ? MF_CHECKED : 0), RepopulateMenu_AllocateMenuId([this, &sqpacks, &nestedTtmp]() {
				try {
					nestedTtmp.Enabled = !nestedTtmp.Enabled;
					sqpacks->ApplyTtmpChanges(nestedTtmp);
				} catch (const std::exception& e) {
					Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
				}
				}), RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_TTMP_ENTRY_ENABLE).c_str());

			AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, &ttmpSet, &sqpacks]() {
				try {
					if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2,
						L"Delete \"{}\" at \"{}\"?", ttmpSet.List.Name, ttmpSet.ListPath.wstring()) == IDYES)
						sqpacks->DeleteTtmp(ttmpSet.ListPath);
				} catch (const std::exception& e) {
					Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
				}
				}), RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_TTMP_ENTRY_DELETE).c_str());

			AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(hSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, &ttmpSet]() {
				Utils::Win32::TaskDialog::Builder()
					.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
					.WithParentWindow(m_hWnd)
					.WithInstance(Dll::Module())
					.WithAllowDialogCancellation()
					.WithCanBeMinimized()
					.WithHyperlinkHandler(L"homepage", [&ttmpSet](auto& dialog) {
						try {
							Utils::Win32::ShellExecutePathOrThrow(Utils::FromUtf8(ttmpSet.List.Url), dialog.GetHwnd());
						} catch (const std::exception& e) {
							Dll::MessageBoxF(dialog.GetHwnd(), MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
						}
						return Utils::Win32::TaskDialog::HyperlinkHandleResult::HandledKeepDialog;
						})
					.WithMainIcon(IDI_TRAY_ICON)
					.WithMainInstruction(ttmpSet.List.Name)
					.WithContent(std::format(L"{} - {}{}",
						ttmpSet.List.Version.empty() ? "0.0" : ttmpSet.List.Version,
						ttmpSet.List.Author.empty() ? "Anonymous" : ttmpSet.List.Author,
						ttmpSet.List.Description.empty() ? "" : std::format("\n\n{}", ttmpSet.List.Description)
					))
					.WithFooter(ttmpSet.List.Url.empty() ? L"" : std::format(
						L"<a href=\"homepage\">{}</a>",
						ttmpSet.List.Url
					))
					.Build()
					.Show();
				}), std::format(L"{} - {} ({})",
					ttmpSet.List.Name.empty() ? "Unnamed" : ttmpSet.List.Name,
					ttmpSet.List.Author.empty() ? "Anonymous" : ttmpSet.List.Author,
					ttmpSet.List.Version.empty() ? "0.0" : ttmpSet.List.Version
				).c_str());
			if (!ttmpSet.Allocated) {
				AppendMenuW(hSubMenu, MF_STRING | MF_DISABLED, 0, RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_TTMP_ENTRY_REQUIRESRESTART).c_str());
			}

			if (!ttmpSet.List.ModPackPages.empty()) {
				AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);
			}

			for (size_t pageObjectIndex = 0; pageObjectIndex < ttmpSet.List.ModPackPages.size(); ++pageObjectIndex) {
				const auto& modGroups = ttmpSet.List.ModPackPages[pageObjectIndex].ModGroups;
				if (modGroups.empty())
					continue;
				const auto& pageConf = ttmpSet.Choices.at(pageObjectIndex);

				for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
					const auto& modGroup = modGroups[modGroupIndex];
					if (modGroup.OptionList.empty())
						continue;

					const auto isMulti = modGroup.SelectionType == "Multi";
					const auto optionIndices = pageConf.at(modGroupIndex).get<std::set<size_t>>();

					const auto hModSubMenu = CreatePopupMenu();

					if (std::ranges::any_of(modGroup.OptionList, [](const auto& e) { return !e.Description.empty(); })) {
						AppendMenuW(hModSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, &modGroup]() {
							std::string description;
							for (const auto& option : modGroup.OptionList) {
								if (!description.empty())
									description += "\n";
								description += std::format("* {}: {}", option.Name, option.Description.empty() ? "-" : option.Description);
							}
							void(Utils::Win32::Thread(L"MsgBoxThread", [description, &groupName = modGroup.GroupName]() {
								MessageBoxW(nullptr, Utils::FromUtf8(description).c_str(), Utils::FromUtf8(groupName).c_str(), MB_OK);
							}));
							}), RepopulateMenu_GetMenuTextById(hTemplateEntryMenu, ID_MODDING_TTMP_ENTRY_SHOWDESCRIPTION).c_str());
						AppendMenuW(hModSubMenu, MF_SEPARATOR, 0, nullptr);
					}

					for (size_t optionIndex = 0; optionIndex < modGroup.OptionList.size(); ++optionIndex) {
						const auto& modEntry = modGroup.OptionList[optionIndex];

						std::string description = modEntry.Name.empty() ? "-" : modEntry.Name;
						if (!modEntry.GroupName.empty() && modEntry.GroupName != modGroup.GroupName)
							description += std::format(" ({})", modEntry.GroupName);

						AppendMenuW(hModSubMenu, MF_STRING | (optionIndices.contains(optionIndex) ? MF_CHECKED : 0), RepopulateMenu_AllocateMenuId(
							[this, isMulti, pageObjectIndex, modGroupIndex, optionIndices = optionIndices, optionIndex, &ttmpSet, &nestedTtmp, &sqpacks]() mutable {
								try {
									auto& page = ttmpSet.Choices.at(pageObjectIndex);

									if (isMulti || (GetKeyState(VK_CONTROL) & 0x8000U)) {
										if (optionIndices.contains(optionIndex))
											optionIndices.erase(optionIndex);
										else
											optionIndices.insert(optionIndex);
										page[modGroupIndex] = optionIndices;
									} else
										page[modGroupIndex] = nlohmann::json::array({ optionIndex });

									sqpacks->ApplyTtmpChanges(nestedTtmp);

								} catch (const std::exception& e) {
									Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
								}
							}), Utils::FromUtf8(description).c_str());
					}

					if (isMulti) {
						AppendMenuW(hModSubMenu, MF_SEPARATOR, 0, nullptr);
						AppendMenuW(hModSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, pageObjectIndex, modGroupIndex, &modGroup, &ttmpSet, &nestedTtmp, &sqpacks]() {
							try {
								auto& page = ttmpSet.Choices.at(pageObjectIndex);
								auto newOptions = nlohmann::json::array();
								for (size_t i = 0; i < modGroup.OptionList.size(); ++i)
									newOptions.push_back(i);
								page[modGroupIndex] = std::move(newOptions);
								sqpacks->ApplyTtmpChanges(nestedTtmp);
							} catch (const std::exception& e) {
								Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
							}
							}), RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_ENABLEALL).c_str());
						AppendMenuW(hModSubMenu, MF_STRING, RepopulateMenu_AllocateMenuId([this, pageObjectIndex, modGroupIndex, &ttmpSet, &nestedTtmp, &sqpacks]() {
							try {
								auto& page = ttmpSet.Choices.at(pageObjectIndex);
								page[modGroupIndex] = nlohmann::json::array();
								sqpacks->ApplyTtmpChanges(nestedTtmp);
							} catch (const std::exception& e) {
								Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
							}
							}), RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_DISABLEALL).c_str());
					}

					AppendMenuW(hSubMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hModSubMenu), Utils::FromUtf8(modGroup.GroupName).c_str());
				}
			}

			if (m_config->Runtime.TtmpFlattenSubdirectoryDisplay) {
				std::wstring menuName;
				menuName.resize(3 * (menuStack.size() - 1), L' ');
				menuName += nestedTtmp.Path.filename().wstring();

				InsertMenuW(menuStack.front().Menu,
					menuStack.front().InsertionIndex++,
					MF_BYPOSITION | MF_STRING | MF_POPUP | (nestedTtmp.Enabled ? MF_CHECKED : 0),
					reinterpret_cast<UINT_PTR>(hSubMenu),
					menuName.c_str());
			} else {
				InsertMenuW(menuStack.back().Menu,
					menuStack.back().InsertionIndex++,
					MF_BYPOSITION | MF_STRING | MF_POPUP | (nestedTtmp.Enabled ? MF_CHECKED : 0),
					reinterpret_cast<UINT_PTR>(hSubMenu),
					nestedTtmp.Path.filename().wstring().c_str());
			}
			});
	}
	if (m_config->Runtime.TtmpShowDedicatedMenu) {
		if (!ready)
			AppendMenuW(hOuterTtmpMenu, MF_DISABLED, 0, RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_NOTREADY).c_str());
		DeleteMenu(hInnerTtmpMenu, ID_MODDING_TTMP_NOTREADY, MF_BYCOMMAND);

		if (!count && ready)
			AppendMenuW(hOuterTtmpMenu, MF_DISABLED, 0, RepopulateMenu_GetMenuTextById(hInnerTtmpMenu, ID_MODDING_TTMP_NOENTRY).c_str());
		DeleteMenu(hInnerTtmpMenu, ID_MODDING_TTMP_NOENTRY, MF_BYCOMMAND);
	} else {
		if (ready)
			DeleteMenu(hInnerTtmpMenu, ID_MODDING_TTMP_NOTREADY, MF_BYCOMMAND);
		if (count || !ready)
			DeleteMenu(hInnerTtmpMenu, ID_MODDING_TTMP_NOENTRY, MF_BYCOMMAND);
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RepopulateMenu_GameFix(HMENU hParentMenu) {
	const auto& patchCodes = m_config->Game.PatchCode.Value();
	if (patchCodes.empty())
		return;

	const auto& digestsVector = m_config->Runtime.EnabledPatchCodes.Value();
	const std::set digests(digestsVector.begin(), digestsVector.end());

	RemoveMenu(hParentMenu, 0, MF_BYPOSITION);
	for (const auto& pc : patchCodes) {
		auto digest = pc.Digest();
		const auto active = digests.contains(digest);
		
		AppendMenuW(hParentMenu, MF_STRING | (active ? MF_CHECKED : 0), RepopulateMenu_AllocateMenuId([this, digest = std::move(digest)]() {
			auto pcs{ m_config->Runtime.EnabledPatchCodes.Value() };
			if (const auto it = std::ranges::find(pcs, digest); it == pcs.end())
				pcs.emplace_back(digest);
			else
				pcs.erase(it);
			m_config->Runtime.EnabledPatchCodes = pcs;
			}), Utils::FromUtf8(pc.Name).c_str());
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::SetMenuStates() const {
	const auto hMenu = GetMenu(m_hWnd);

	const auto& config = m_config->Runtime;
	using namespace Utils::Win32;

	// File
	{
		SetMenuState(hMenu, ID_FILE_SHOWCONTROLWINDOW, config.ShowControlWindow, true);
		SetMenuState(hMenu, ID_FILE_SHOWLOGGINGWINDOW, config.ShowLoggingWindow, true);
	}

	// Game
	{
		SetMenuState(hMenu, ID_RESTART_RESTART, false, !m_launchParameters.empty());
		SetMenuState(hMenu, ID_RESTART_USEDIRECTX11, m_bUseDirectX11, !m_launchParameters.empty());
		SetMenuState(hMenu, ID_RESTART_USEXIVALEXANDER, m_bUseXivAlexander, !m_launchParameters.empty());
		SetMenuState(hMenu, ID_RESTART_USEPARAMETEROBFUSCATION, m_bUseParameterObfuscation, !m_launchParameters.empty());
		SetMenuState(hMenu, ID_RESTART_USEELEVATION, m_bUseElevation, !m_launchParameters.empty());
		const auto languageRegionModifiable = Dll::IsLanguageRegionModifiable();
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_REMEMBER, languageRegionModifiable && m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_ENGLISH, m_gameLanguage == Sqex::Language::English, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_GERMAN, m_gameLanguage == Sqex::Language::German, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_FRENCH, m_gameLanguage == Sqex::Language::French, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_JAPANESE, m_gameLanguage == Sqex::Language::Japanese, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE, m_gameLanguage == Sqex::Language::ChineseSimplified, false);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_KOREAN, m_gameLanguage == Sqex::Language::Korean, false);
		SetMenuState(hMenu, ID_RESTART_REGION_REMEMBER, languageRegionModifiable && m_config->Runtime.RememberedGameLaunchRegion != Sqex::Region::Unspecified, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_REGION_JAPAN, m_gameRegion == Sqex::Region::Japan, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_REGION_NORTH_AMERICA, m_gameRegion == Sqex::Region::NorthAmerica, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_REGION_EUROPE, m_gameRegion == Sqex::Region::Europe, languageRegionModifiable);
	}

	// Network
	{
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE, config.UseNetworkTimingHandler, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1, config.HighLatencyMitigationMode == HighLatencyMitigationMode::SubtractLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2, config.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateRtt, true, config.FormatStringRes(IDS_MENU_NETWORKLATENCYHANDLEMODE_2, config.ExpectedAnimationLockDurationUs.Value()));
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3, config.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateNormalizedRttAndLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING, config.UseHighLatencyMitigationLogging, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE, config.UseHighLatencyMitigationPreviewMode, true);
		SetMenuState(hMenu, ID_NETWORK_USEIPCTYPEFINDER, config.UseOpcodeFinder, true);
		SetMenuState(hMenu, ID_NETWORK_USEALLIPCMESSAGELOGGER, config.UseAllIpcMessageLogger, true);
		SetMenuState(hMenu, ID_NETWORK_REDUCEPACKETDELAY, config.ReducePacketDelay, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES, config.TakeOverLoopbackAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES, config.TakeOverPrivateAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES, config.TakeOverAllAddresses, true);
		SetMenuState(hMenu, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS, config.TakeOverAllPorts, true);
	}

	// Modding
	{
		SetMenuState(hMenu, ID_MODDING_ENABLE, config.UseModding, true);
		SetMenuState(hMenu, ID_MODDING_LOGALLHASHKEYS, config.UseHashTrackerKeyLogging, true);
		SetMenuState(hMenu, ID_MODDING_LOGALLFILEACCESS, config.LogAllDataFileRead, true);
		SetMenuState(hMenu, ID_MODDING_COMPRESSWHENEVERPOSSIBLE, config.CompressModdedFiles, true);

		const auto languageList = config.GetFallbackLanguageList();
		SetMenuState(hMenu, ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY1, false, true, config.GetLanguageNameLocalized(languageList[0]));
		SetMenuState(hMenu, ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY2, false, true, config.GetLanguageNameLocalized(languageList[1]));
		SetMenuState(hMenu, ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY3, false, true, config.GetLanguageNameLocalized(languageList[2]));
		SetMenuState(hMenu, ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY4, false, true, config.GetLanguageNameLocalized(languageList[3]));
		SetMenuState(hMenu, ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY5, false, true, config.GetLanguageNameLocalized(languageList[4]));
		SetMenuState(hMenu, ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY6, false, true, config.GetLanguageNameLocalized(languageList[5]));

		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_DISABLE, config.ResourceLanguageOverride == Sqex::Language::Unspecified, true);
		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_ENGLISH, config.ResourceLanguageOverride == Sqex::Language::English, true);
		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_GERMAN, config.ResourceLanguageOverride == Sqex::Language::German, true);
		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_FRENCH, config.ResourceLanguageOverride == Sqex::Language::French, true);
		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_JAPANESE, config.ResourceLanguageOverride == Sqex::Language::Japanese, true);
		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_CHINESESIMPLIFIED, config.ResourceLanguageOverride == Sqex::Language::ChineseSimplified, true);
		SetMenuState(hMenu, ID_MODDING_DISPLAYLANGUAGE_KOREAN, config.ResourceLanguageOverride == Sqex::Language::Korean, true);

		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_DISABLE, config.VoiceResourceLanguageOverride == Sqex::Language::Unspecified, true);
		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_ENGLISH, config.VoiceResourceLanguageOverride == Sqex::Language::English, true);
		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_GERMAN, config.VoiceResourceLanguageOverride == Sqex::Language::German, true);
		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_FRENCH, config.VoiceResourceLanguageOverride == Sqex::Language::French, true);
		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_JAPANESE, config.VoiceResourceLanguageOverride == Sqex::Language::Japanese, true);
		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_CHINESESIMPLIFIED, config.VoiceResourceLanguageOverride == Sqex::Language::ChineseSimplified, true);
		SetMenuState(hMenu, ID_MODDING_AUDIOLANGUAGE_KOREAN, config.VoiceResourceLanguageOverride == Sqex::Language::Korean, true);

		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_BATTLE, config.MuteVoice_Battle, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_CM, config.MuteVoice_Cm, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_EMOTE, config.MuteVoice_Emote, true);
		SetMenuState(hMenu, ID_MODDING_MUTEVOICE_LINE, config.MuteVoice_Line, true);

		SetMenuState(hMenu, ID_MODDING_REPLACEMUSICS_SAMPLINGRATE_HIGHESTPOSSIBLE, config.MusicImportTargetSamplingRate == 0, true);
		SetMenuState(hMenu, ID_MODDING_REPLACEMUSICS_SAMPLINGRATE_44100, config.MusicImportTargetSamplingRate == 44100, true);
		SetMenuState(hMenu, ID_MODDING_REPLACEMUSICS_SAMPLINGRATE_48000, config.MusicImportTargetSamplingRate == 48000, true);

		SetMenuState(hMenu, ID_MODDING_TTMP_FLATTENSUBDIRECTORYDISPLAY, config.TtmpFlattenSubdirectoryDisplay, true);
		SetMenuState(hMenu, ID_MODDING_TTMP_USESUBDIRECTORYTOGGLINGONFLATTENEDVIEW, config.TtmpUseSubdirectoryTogglingOnFlattenedView, config.TtmpFlattenSubdirectoryDisplay);
		SetMenuState(hMenu, ID_MODDING_TTMP_SHOWDEDICATEDMENU, config.TtmpShowDedicatedMenu, true);

		SetMenuState(hMenu, ID_MODDING_CHANGEFONT_DISABLE, config.OverrideFontConfig.Value().empty(), true);
	}

	// Configure
	{
		SetMenuState(hMenu, ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP, config.CheckForUpdatedOpcodesOnStartup, true);
		SetMenuState(hMenu, ID_CONFIGURE_USEMORECPUTIME, config.UseMoreCpuTime, true);
		if (config.LockFramerateAutomatic)
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, true, true, m_config->Runtime.GetStringRes(IDS_MENU_LOCKFRAMERATE_AUTOMATIC));
		else if (config.LockFramerateInterval)
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, true, true, m_config->Runtime.FormatStringRes(IDS_MENU_LOCKFRAMERATE, 1000000. / config.LockFramerateInterval));
		else
			SetMenuState(hMenu, ID_CONFIGURE_LOCKFRAMERATE, false, true, m_config->Runtime.GetStringRes(IDS_MENU_LOCKFRAMERATE_DISABLED));
		SetMenuState(hMenu, ID_CONFIGURE_SYNCHRONIZEPROCESSING, config.SynchronizeProcessing, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT, config.Language == Language::SystemDefault, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_ENGLISH, config.Language == Language::English, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_KOREAN, config.Language == Language::Korean, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_JAPANESE, config.Language == Language::Japanese, true);
	}

	// View
	{
		SetMenuState(hMenu, ID_VIEW_ALWAYSONTOP, config.AlwaysOnTop_XivAlexMainWindow, true);
		SetMenuState(hMenu, ID_VIEW_ALWAYSONTOPGAME, config.AlwaysOnTop_GameMainWindow, true);
		SetMenuState(hMenu, ID_VIEW_HIDEONMINIMIZE, config.HideOnMinimize_XivAlexMainWindow, true);
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::RegisterTrayIcon() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"LoadIconW");
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
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
	NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

void XivAlexander::Apps::MainApp::Window::MainWindow::AskRestartGame(bool onlyOnModifier) {
	if (onlyOnModifier && !((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000))) {
		return;
	}
	const auto yes = Utils::Win32::MB_GetString(IDYES - 1);
	const auto no = Utils::Win32::MB_GetString(IDNO - 1);
	if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, m_config->Runtime.FormatStringRes(
		IDS_CONFIRM_RESTART_GAME,
		m_bUseDirectX11 ? yes : no,
		m_bUseXivAlexander ? yes : no,
		m_bUseParameterObfuscation ? yes : no,
		m_bUseElevation ? yes : no,
		m_config->Runtime.GetLanguageNameLocalized(m_gameLanguage),
		m_config->Runtime.GetRegionNameLocalized(m_gameRegion)
	)) == IDYES) {
		auto params{ m_launchParameters };
		if (Dll::IsLanguageRegionModifiable()) {
			Sqex::CommandLine::WellKnown::SetLanguage(params, m_gameLanguage);
			Sqex::CommandLine::WellKnown::SetRegion(params, m_gameRegion);
		}

		Utils::Win32::RunProgramParams runParams{
			.path = Utils::Win32::Process::Current().PathOf().parent_path() / (m_bUseDirectX11 ? Dll::GameExecutable64NameW : Dll::GameExecutable32NameW),
			.args = Sqex::CommandLine::ToString(params, m_bUseParameterObfuscation),
			.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
		};

		Dll::EnableInjectOnCreateProcess(0);
		const auto revertInjectOnCreateProcess = Utils::CallOnDestruction([]() { Dll::EnableInjectOnCreateProcess(Dll::InjectOnCreateProcessAppFlags::Use | Dll::InjectOnCreateProcessAppFlags::InjectGameOnly); });

		// This only mattered on initialization at AutoLoadAsDependencyModule, so it's safe to modify and not revert
		if (m_bUseXivAlexander)
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);
		else
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", L"1");

		if (!Dll::IsLoadedAsDependency() && m_bUseXivAlexander) {
			runParams.args = std::format(L"-a launcher -l select {} {}", Utils::Win32::ReverseCommandLineToArgv(runParams.path), runParams.args);
			runParams.path = Dll::Module().PathOf().parent_path() / (m_bUseDirectX11 ? Dll::XivAlexLoader64NameW : Dll::XivAlexLoader32NameW);
		}

		if (Utils::Win32::RunProgram(std::move(runParams))) {
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

		case ID_FILE_SHOWLOGGINGWINDOW:
			config.ShowLoggingWindow.Toggle();
			return;

		case ID_FILE_SHOWCONTROLWINDOW:
			config.ShowControlWindow.Toggle();
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

		case ID_RESTART_USEDIRECTX11:
			m_bUseDirectX11 = !m_bUseDirectX11;
			AskRestartGame(true);
			return;

		case ID_RESTART_USEXIVALEXANDER:
			m_bUseXivAlexander = !m_bUseXivAlexander;
			AskRestartGame(true);
			return;

		case ID_RESTART_USEPARAMETEROBFUSCATION:
			m_bUseParameterObfuscation = !m_bUseParameterObfuscation;
			AskRestartGame(true);
			return;

		case ID_RESTART_USEELEVATION:
			m_bUseElevation = !m_bUseElevation;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_REMEMBER:
			if (m_config->Runtime.RememberedGameLaunchLanguage == Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			else
				m_config->Runtime.RememberedGameLaunchLanguage = Sqex::Language::Unspecified;
			return;

		case ID_RESTART_LANGUAGE_ENGLISH:
			m_gameLanguage = Sqex::Language::English;
			if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_GERMAN:
			m_gameLanguage = Sqex::Language::German;
			if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_FRENCH:
			m_gameLanguage = Sqex::Language::French;
			if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_JAPANESE:
			m_gameLanguage = Sqex::Language::Japanese;
			if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE:
			m_gameLanguage = Sqex::Language::ChineseSimplified;
			if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_KOREAN:
			m_gameLanguage = Sqex::Language::Korean;
			if (m_config->Runtime.RememberedGameLaunchLanguage != Sqex::Language::Unspecified)
				m_config->Runtime.RememberedGameLaunchLanguage = m_gameLanguage;
			AskRestartGame(true);
			return;

		case ID_RESTART_REGION_REMEMBER:
			if (m_config->Runtime.RememberedGameLaunchRegion == Sqex::Region::Unspecified)
				m_config->Runtime.RememberedGameLaunchRegion = m_gameRegion;
			else
				m_config->Runtime.RememberedGameLaunchRegion = Sqex::Region::Unspecified;
			return;

		case ID_RESTART_REGION_JAPAN:
			m_gameRegion = Sqex::Region::Japan;
			if (m_config->Runtime.RememberedGameLaunchRegion != Sqex::Region::Unspecified)
				m_config->Runtime.RememberedGameLaunchRegion = m_gameRegion;
			AskRestartGame(true);
			return;

		case ID_RESTART_REGION_NORTH_AMERICA:
			m_gameRegion = Sqex::Region::NorthAmerica;
			if (m_config->Runtime.RememberedGameLaunchRegion != Sqex::Region::Unspecified)
				m_config->Runtime.RememberedGameLaunchRegion = m_gameRegion;
			AskRestartGame(true);
			return;

		case ID_RESTART_REGION_EUROPE:
			m_gameRegion = Sqex::Region::Europe;
			if (m_config->Runtime.RememberedGameLaunchRegion != Sqex::Region::Unspecified)
				m_config->Runtime.RememberedGameLaunchRegion = m_gameRegion;
			AskRestartGame(true);
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Network(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE:
			config.UseNetworkTimingHandler.Toggle();
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1:
			config.HighLatencyMitigationMode = HighLatencyMitigationMode::SubtractLatency;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2:
			config.HighLatencyMitigationMode = HighLatencyMitigationMode::SimulateRtt;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3:
			config.HighLatencyMitigationMode = HighLatencyMitigationMode::SimulateNormalizedRttAndLatency;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING:
			config.UseHighLatencyMitigationLogging.Toggle();
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE:
			config.UseHighLatencyMitigationPreviewMode.Toggle();
			return;

		case ID_NETWORK_USEALLIPCMESSAGELOGGER:
			config.UseAllIpcMessageLogger.Toggle();
			return;

		case ID_NETWORK_USEIPCTYPEFINDER:
			config.UseOpcodeFinder.Toggle();
			return;

		case ID_NETWORK_REDUCEPACKETDELAY:
			config.ReducePacketDelay.Toggle();
			return;

		case ID_NETWORK_RELEASEALLCONNECTIONS:
			m_app.RunOnGameLoop([this]() {
				m_app.GetSocketHook().ReleaseSockets();
				});
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES:
			config.TakeOverLoopbackAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES:
			config.TakeOverPrivateAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES:
			config.TakeOverAllAddresses.Toggle();
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS:
			config.TakeOverAllPorts.Toggle();
			return;
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Modding(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_MODDING_ENABLE:
			config.UseModding.Toggle();
			return;

		case ID_MODDING_LOGALLHASHKEYS:
			config.UseHashTrackerKeyLogging.Toggle();
			return;

		case ID_MODDING_LOGALLFILEACCESS:
			config.LogAllDataFileRead.Toggle();
			return;

		case ID_MODDING_COMPRESSWHENEVERPOSSIBLE:
			config.CompressModdedFiles.Toggle();
			return;

		case ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY1: {
			auto languageList = config.GetFallbackLanguageList();
			languageList.insert(languageList.begin(), languageList[0]);
			languageList.erase(languageList.begin() + 1);
			config.FallbackLanguagePriority = languageList;
			return;
		}
		case ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY2: {
			auto languageList = config.GetFallbackLanguageList();
			languageList.insert(languageList.begin(), languageList[1]);
			languageList.erase(languageList.begin() + 2);
			config.FallbackLanguagePriority = languageList;
			return;
		}
		case ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY3: {
			auto languageList = config.GetFallbackLanguageList();
			languageList.insert(languageList.begin(), languageList[2]);
			languageList.erase(languageList.begin() + 3);
			config.FallbackLanguagePriority = languageList;
			return;
		}
		case ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY4: {
			auto languageList = config.GetFallbackLanguageList();
			languageList.insert(languageList.begin(), languageList[3]);
			languageList.erase(languageList.begin() + 4);
			config.FallbackLanguagePriority = languageList;
			return;
		}
		case ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY5: {
			auto languageList = config.GetFallbackLanguageList();
			languageList.insert(languageList.begin(), languageList[4]);
			languageList.erase(languageList.begin() + 5);
			config.FallbackLanguagePriority = languageList;
			return;
		}
		case ID_MODDING_FALLBACKLANGUAGEPRIORITY_ENTRY6: {
			auto languageList = config.GetFallbackLanguageList();
			languageList.insert(languageList.begin(), languageList[5]);
			languageList.erase(languageList.begin() + 6);
			config.FallbackLanguagePriority = languageList;
			return;
		}

		case ID_MODDING_DISPLAYLANGUAGE_DISABLE:
			config.ResourceLanguageOverride = Sqex::Language::Unspecified;
			return;

		case ID_MODDING_DISPLAYLANGUAGE_ENGLISH:
			config.ResourceLanguageOverride = Sqex::Language::English;
			return;

		case ID_MODDING_DISPLAYLANGUAGE_GERMAN:
			config.ResourceLanguageOverride = Sqex::Language::German;
			return;

		case ID_MODDING_DISPLAYLANGUAGE_FRENCH:
			config.ResourceLanguageOverride = Sqex::Language::French;
			return;

		case ID_MODDING_DISPLAYLANGUAGE_JAPANESE:
			config.ResourceLanguageOverride = Sqex::Language::Japanese;
			return;

		case ID_MODDING_DISPLAYLANGUAGE_CHINESESIMPLIFIED:
			config.ResourceLanguageOverride = Sqex::Language::ChineseSimplified;
			return;

		case ID_MODDING_DISPLAYLANGUAGE_KOREAN:
			config.ResourceLanguageOverride = Sqex::Language::Korean;
			return;

		case ID_MODDING_AUDIOLANGUAGE_DISABLE:
			config.VoiceResourceLanguageOverride = Sqex::Language::Unspecified;
			return;

		case ID_MODDING_AUDIOLANGUAGE_ENGLISH:
			config.VoiceResourceLanguageOverride = Sqex::Language::English;
			return;

		case ID_MODDING_AUDIOLANGUAGE_GERMAN:
			config.VoiceResourceLanguageOverride = Sqex::Language::German;
			return;

		case ID_MODDING_AUDIOLANGUAGE_FRENCH:
			config.VoiceResourceLanguageOverride = Sqex::Language::French;
			return;

		case ID_MODDING_AUDIOLANGUAGE_JAPANESE:
			config.VoiceResourceLanguageOverride = Sqex::Language::Japanese;
			return;

		case ID_MODDING_AUDIOLANGUAGE_CHINESESIMPLIFIED:
			config.VoiceResourceLanguageOverride = Sqex::Language::ChineseSimplified;
			return;

		case ID_MODDING_AUDIOLANGUAGE_KOREAN:
			config.VoiceResourceLanguageOverride = Sqex::Language::Korean;
			return;

		case ID_MODDING_MUTEVOICE_BATTLE:
			config.MuteVoice_Battle.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_CM:
			config.MuteVoice_Cm.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_EMOTE:
			config.MuteVoice_Emote.Toggle();
			return;

		case ID_MODDING_MUTEVOICE_LINE:
			config.MuteVoice_Line.Toggle();
			return;

		case ID_MODDING_CHANGEFONT_OPENPRESETDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath() / "FontConfig");
			return;

		case ID_MODDING_CHANGEFONT_IMPORTPRESET: {
			const auto defaultPath = m_config->Init.ResolveConfigStorageDirectoryPath() / "FontConfig";
			try {
				if (!is_directory(defaultPath))
					create_directories(defaultPath);
			} catch (...) {
				// pass
			}

			const COMDLG_FILTERSPEC fileTypes[] = {
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_FONTPRESETJSON), L"*.json" },
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*" },
			};
			const auto paths = ChooseFileToOpen(std::span(fileTypes), IDS_TITLE_IMPORT_FONTCONFIG_PRESET, defaultPath);
			switch (paths.size()) {
				case 0:
					return;

				case 1:
					ImportFontConfig(paths[0]);
					return;

				default:
					InstallMultipleFiles(paths);
			}
			return;
		}

		case ID_MODDING_CHANGEFONT_DISABLE:
			m_config->Runtime.OverrideFontConfig = std::filesystem::path();
			return;

		case ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_ADD_SELECTFOLDER:
			while (true) {
				try {
					IFileOpenDialogPtr pDialog;
					DWORD dwFlags;
					Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
					Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(m_config->Runtime.GetStringRes(IDS_TITLE_ADD_EXTERNAL_GAME_DIRECTORY)));
					Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
					Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS));
					Utils::Win32::Error::ThrowIfFailed(pDialog->Show(m_hWnd), true);

					IShellItemPtr pResult;
					PWSTR pszFileName;
					Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
					Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
					if (!pszFileName)
						throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
					const auto freeFileName = Utils::CallOnDestruction([pszFileName]() { CoTaskMemFree(pszFileName); });

					AddAdditionalGameRootDirectory(pszFileName);
					break;

				} catch (const Utils::Win32::CancelledError&) {
					break;

				} catch (const std::exception& e) {
					Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
				}
			}
			return;

		case ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_REMOVEALL:
			if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, IDS_CONFIRM_UNREGISTERALLADDITIONALGAMEROOTDIRECTORIES) == IDYES) {
				m_config->Runtime.AdditionalSqpackRootDirectories = std::vector<std::filesystem::path>();
			}
			return;

		case ID_MODDING_EXDFTRANSFORMATIONRULES_OPENPRESETDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath() / "ExcelTransformConfig");
			return;

		case ID_MODDING_EXDFTRANSFORMATIONRULES_ADD: {
			const auto defaultPath = m_config->Init.ResolveConfigStorageDirectoryPath() / "ExcelTransformConfig";
			try {
				if (!is_directory(defaultPath))
					create_directories(defaultPath);
			} catch (...) {
				// pass
			}

			static const COMDLG_FILTERSPEC fileTypes[] = {
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_EXCELTRANSFORMCONFIGJSON), L"*.json" },
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*" },
			};
			const auto paths = ChooseFileToOpen(std::span(fileTypes), IDS_TITLE_ADD_EXCELTRANSFORMCONFIG, defaultPath);
			switch (paths.size()) {
				case 0:
					return;

				case 1:
					ImportExcelTransformConfig(paths[0]);
					return;

				default:
					InstallMultipleFiles(paths);
			}
			return;
		}

		case ID_MODDING_EXDFTRANSFORMATIONRULES_REMOVEALL:
			if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, IDS_CONFIRM_UNREGISTERALLEXCELTRANSFORMATIONRULES) == IDYES) {
				m_config->Runtime.ExcelTransformConfigFiles = std::vector<std::filesystem::path>();
			}
			return;

		case ID_MODDING_REPLACEMUSICS_IMPORT: {
			const auto defaultPath = m_config->Init.ResolveConfigStorageDirectoryPath() / "MusicImportConfig";
			try {
				if (!is_directory(defaultPath))
					create_directories(defaultPath);
			} catch (...) {
				// pass
			}

			static const COMDLG_FILTERSPEC fileTypes[] = {
				{ L"Music Import Config (*.json)", L"*.json" },
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*" },
			};
			const auto paths = ChooseFileToOpen(std::span(fileTypes), IDS_TITLE_ADD_MUSICIMPORTCONFIG, defaultPath);
			switch (paths.size()) {
				case 0:
					return;

				case 1:
					ImportMusicImportConfig(paths[0]);
					return;

				default:
					InstallMultipleFiles(paths);
			}
			return;
		}

		case ID_MODDING_REPLACEMUSICS_REMOVEALL: {
			if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, IDS_CONFIRM_UNREGISTERALLMUSICIMPORTCONFIG) == IDYES) {
				m_config->Runtime.MusicImportConfig = std::vector<std::filesystem::path>{};
			}
			return;
		}

		case ID_MODDING_REPLACEMUSICS_GENERATE: {
			if (m_backgroundWorkerThread) {
				const auto window = decltype(m_backgroundWorkerProgressWindow)(m_backgroundWorkerProgressWindow);
				if (window && window->GetCancelEvent().Wait(0) == WAIT_TIMEOUT)
					SetForegroundWindow(m_backgroundWorkerProgressWindow->Handle());
				else
					Dll::MessageBoxF(m_hWnd, MB_ICONWARNING, IDS_ERROR_CANCELLING_TRYAGAINLATER);
				return;
			}

			auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks();
			if (!sqpacks) {
				Dll::MessageBoxF(m_hWnd, MB_ICONWARNING, IDS_ERROR_MODDINGACTIVEREQUIRED);
				return;
			}

			auto ffmpeg = m_config->Init.ResolveXivAlexInstallationPath() / L"ffmpeg.exe";
			auto ffprobe = m_config->Init.ResolveXivAlexInstallationPath() / L"ffprobe.exe";

			if (!exists(ffmpeg) || !exists(ffprobe)) {
				const auto res = Utils::Win32::TaskDialog::Builder()
					.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
					.WithParentWindow(m_hWnd)
					.WithInstance(Dll::Module())
					.WithAllowDialogCancellation()
					.WithCanBeMinimized()
					.WithHyperlinkShellExecute()
					.WithMainIcon(IDI_TRAY_ICON)
					.WithMainInstruction(m_config->Runtime.GetStringRes(IDS_FFMPEGREQUIRED_MAININSTRUCTIONS))
					.WithContent(m_config->Runtime.GetStringRes(IDS_FFMPEGREQUIRED_CONTENT))
					.WithButton({
						.Id = 1001,
						.Text = m_config->Runtime.GetStringRes(IDS_FFMPEGREQUIRED_OPENDOWNLOADSITE),
						.Callback = [](const auto& dialog) {
							try {
								Utils::Win32::ShellExecutePathOrThrow(L"https://ffmpeg.org/download.html", dialog.GetHwnd());
							} catch (const std::exception& e) {
								Dll::MessageBoxF(dialog.GetHwnd(), MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
							}
							return Utils::Win32::TaskDialog::ActionHandled::Handled;
						},
						})
					.WithButton({
						.Id = 1002,
						.Text = m_config->Runtime.GetStringRes(IDS_FFMPEGREQUIRED_OPENTARGETDIR),
						.Callback = [this](const auto& dialog) {
							try {
								Utils::Win32::ShellExecutePathOrThrow(m_config->Init.ResolveXivAlexInstallationPath(), dialog.GetHwnd());
							} catch (const std::exception& e) {
								Dll::MessageBoxF(dialog.GetHwnd(), MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
							}
							return Utils::Win32::TaskDialog::ActionHandled::Handled;
						},
						})
					.WithButton({
						.Id = 1003,
						.Text = m_config->Runtime.GetStringRes(IDS_FFMPEGREQUIRED_CONFIRMINSTALLATION),
						.Callback = [&ffmpeg, &ffprobe](const auto& dialog) {
							if (!exists(ffmpeg)) {
								Dll::MessageBoxF(dialog.GetHwnd(), MB_OK | MB_ICONWARNING, IDS_FFMPEGREQUIRED_NOTFOUND, ffmpeg);
								return Utils::Win32::TaskDialog::ActionHandled::Handled;
							}
							if (!exists(ffprobe)) {
								Dll::MessageBoxF(dialog.GetHwnd(), MB_OK | MB_ICONWARNING, IDS_FFMPEGREQUIRED_NOTFOUND, ffprobe);
								return Utils::Win32::TaskDialog::ActionHandled::Handled;
							}
							return Utils::Win32::TaskDialog::ActionHandled::NotHandled;
						},
						})
					.WithButtonDefault(1001)
							.Build()
							.Show();
						if (res.Button == IDCANCEL)
							return;
			}

			m_backgroundWorkerThread = Utils::Win32::Thread(L"ReplaceMusicGeneratorOnOtherThread", [this, &sqpacks, ffmpeg = ffmpeg, ffprobe = ffprobe] {
				const auto targetBasePath = m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries";

				m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);

				size_t index = 0;
				size_t count = 0;
				const std::filesystem::path* pLastStartedTargetFile = nullptr;
				const auto workerThread = Utils::Win32::Thread(L"ReplaceMusicGenerator", [&] {
					std::vector<Sqex::Sound::MusicImportItem> items;
					for (const auto& confFile : m_config->Runtime.MusicImportConfig.Value()) {
						try {
							Sqex::Sound::MusicImportConfig conf;
							from_json(Utils::ParseJsonFromFile(Config::TranslatePath(confFile)), conf);

							std::string defaultDir;
							for (const auto& [dirName, dirConfig] : conf.searchDirectories) {
								if (dirConfig.default_)
									defaultDir = dirName;
							}

							for (auto& item : conf.items) {
								for (auto& source : item.source | std::views::values) {
									for (auto& fileList : source.inputFiles) {
										for (auto& patternList : fileList) {
											if (!patternList.directory.has_value())
												patternList.directory = defaultDir;
										}
									}
								}
								items.emplace_back(std::move(item));
							}
						} catch (const std::exception& e) {
							m_logger->Format<LogLevel::Warning>(LogCategory::MusicImporter, "{}: {}", confFile.filename().wstring(), e.what());
						}
					}

					auto tp = Utils::Win32::TpEnvironment(L"ReplaceMusicGenerator/pool");
					for (const auto& item : items) {
						for (const auto& target : item.target) {
							if (!target.enable)
								continue;

							auto allTargetExists = true;
							for (const auto& path : target.path)
								allTargetExists &= exists(targetBasePath / path);
							if (allTargetExists)
								continue;

							count++;
							tp.SubmitWork([&] {
								if (m_backgroundWorkerProgressWindow->GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
									return;

								pLastStartedTargetFile = &target.path.front();
								++index;

								try {
									Sqex::Sound::MusicImporter importer(item.source, target, ffmpeg, ffprobe, m_backgroundWorkerProgressWindow->GetCancelEvent());
									const auto logger = importer.OnWarningLog([&](const std::string& s) {
										m_logger->Format<LogLevel::Error>(LogCategory::MusicImporter, "{}: {}\n", target.path.front(), s);
										});

									importer.SetSamplingRate(m_config->Runtime.MusicImportTargetSamplingRate);
									for (const auto& path : target.path)
										importer.AppendReader(std::make_shared<Sqex::Sound::ScdReader>(sqpacks->GetOriginalEntry(path)));
									if (m_backgroundWorkerProgressWindow->GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;

									auto resolved = false;
									for (const auto& [dirName, dirPaths] : m_config->Runtime.MusicImportConfig_Directories.Value()) {
										for (const auto& dirPath : dirPaths)
											resolved |= importer.ResolveSources(dirName, Config::TranslatePath(dirPath));
									}
									if (m_backgroundWorkerProgressWindow->GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;
									if (!resolved)
										throw std::runtime_error("Not all source files are found");

									importer.Merge([&targetBasePath](std::filesystem::path path, std::vector<uint8_t> data) {
										const auto targetPath = targetBasePath / path;
										create_directories(targetPath.parent_path());
										Utils::Win32::Handle::FromCreateFile(targetPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0).Write(0, std::span(data));
										});
									if (m_backgroundWorkerProgressWindow->GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
										return;
								} catch (const std::exception& e) {
									m_logger->Format<LogLevel::Error>(LogCategory::MusicImporter, "{}: {}\n", target.path.front(), e.what());
								}
								});
						}
					}
					tp.WaitOutstanding();
					});

				do {
					m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.FormatStringRes(IDS_TITLE_MUSICIMPORTPROGRESS, pLastStartedTargetFile ? *pLastStartedTargetFile : std::filesystem::path{}, index, count));
					if (index == count)
						m_backgroundWorkerProgressWindow->UpdateProgress(0, 0);
					else
						m_backgroundWorkerProgressWindow->UpdateProgress(index, count);
					m_backgroundWorkerProgressWindow->Show();
				} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
				workerThread.Wait();
				m_backgroundWorkerThread = nullptr;
				m_backgroundWorkerProgressWindow = nullptr;
				});
			return;
		}

		case ID_MODDING_REPLACEMUSICS_SAMPLINGRATE_HIGHESTPOSSIBLE:
			m_config->Runtime.MusicImportTargetSamplingRate = 0;
			return;

		case ID_MODDING_REPLACEMUSICS_SAMPLINGRATE_44100:
			m_config->Runtime.MusicImportTargetSamplingRate = 44100;
			return;

		case ID_MODDING_REPLACEMUSICS_SAMPLINGRATE_48000:
			m_config->Runtime.MusicImportTargetSamplingRate = 48000;
			return;

		case ID_MODDING_TTMP_FLATTENSUBDIRECTORYDISPLAY:
			m_config->Runtime.TtmpFlattenSubdirectoryDisplay.Toggle();
			return;

		case ID_MODDING_TTMP_USESUBDIRECTORYTOGGLINGONFLATTENEDVIEW:
			m_config->Runtime.TtmpUseSubdirectoryTogglingOnFlattenedView.Toggle();
			return;

		case ID_MODDING_TTMP_SHOWDEDICATEDMENU:
			m_config->Runtime.TtmpShowDedicatedMenu.Toggle();
			return;

		case ID_MODDING_TTMP_IMPORT: {
			static const COMDLG_FILTERSPEC fileTypes[] = {
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_TTMP), L"*.ttmp; *.ttmp2; *.mpl" },
				{ m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*" },
			};
			const auto paths = ChooseFileToOpen(std::span(fileTypes), IDS_TITLE_IMPORT_TTMP);
			switch (paths.size()) {
				case 0:
					return;

				case 1: {
					ProgressPopupWindow progress(m_hWnd);
					progress.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_IMPORTING)));
					progress.Show();

					std::string resultMessage;
					const auto adderThread = Utils::Win32::Thread(L"TTMP Importer", [&]() {
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
						Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONINFORMATION, Utils::FromUtf8(resultMessage));
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
			if (auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks())
				BatchTtmpOperation(*sqpacks->GetTtmps(), menuId);
			return;
		}

		case ID_MODDING_TTMP_OPENDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
			return;

		case ID_MODDING_TTMP_REFRESH: {
			if (auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks()) {
				m_backgroundWorkerThread = Utils::Win32::Thread(L"RescanTtmpOnOtherThread", [this, &sqpacks]{
					m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);
					const auto workerThread = Utils::Win32::Thread(L"RescanTtmp", [&] {
						sqpacks->RescanTtmp(*m_backgroundWorkerProgressWindow);
						});

					do {
						m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
						m_backgroundWorkerProgressWindow->Show();
					} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
					workerThread.Wait();
					m_backgroundWorkerThread = nullptr;
					m_backgroundWorkerProgressWindow = nullptr;
					});
			}
			return;
		}

		case ID_MODDING_OPENREPLACEMENTFILEENTRIESDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");
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
				const auto freeFileName = Utils::CallOnDestruction([pszFileName]() { CoTaskMemFree(pszFileName); });

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
					const auto targetBasePath = m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries";
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
							const auto entryPathSpec = Sqex::Sqpack::EntryPathSpec(relPath);
							const auto datFile = entryPathSpec.DatFile();
							if (datFile.empty())
								throw std::runtime_error(std::format("Could not decide where to store {}", relPath));

							std::vector<char> dv;
							if (file_size(target) == 0)
								dv = Sqex::Sqpack::EmptyOrObfuscatedEntryProvider(entryPathSpec).ReadStreamIntoVector<char>(0);
							else if (extensionLower == L".tex" || extensionLower == L".atex")
								dv = Sqex::Sqpack::MemoryTextureEntryProvider(entryPathSpec, std::make_shared<Sqex::FileRandomAccessStream>(target), m_config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION).ReadStreamIntoVector<char>(0);
							else if (extensionLower == L".mdl")
								dv = Sqex::Sqpack::MemoryModelEntryProvider(entryPathSpec, std::make_shared<Sqex::FileRandomAccessStream>(target), m_config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION).ReadStreamIntoVector<char>(0);
							else
								dv = Sqex::Sqpack::MemoryBinaryEntryProvider(entryPathSpec, std::make_shared<Sqex::FileRandomAccessStream>(target), m_config->Runtime.CompressModdedFiles ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION).ReadStreamIntoVector<char>(0);

							if (m_backgroundWorkerProgressWindow->GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
								return;

							const auto entryLine = std::format("{}\n", nlohmann::json::object({
								{"FullPath", Utils::StringReplaceAll<std::string>(Utils::ToUtf8(entryPathSpec.FullPath.wstring()), "\\", "/")},
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
				});
			return;
		}
	}
}

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Configure(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_CONFIGURE_EDITRUNTIMECONFIGURATION:
			if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
				SetForegroundWindow(m_runtimeConfigEditor->Handle());
			else {
				if (m_runtimeConfigEditor)
					delete m_runtimeConfigEditor;
				m_runtimeConfigEditor = new ConfigWindow(IDS_WINDOW_RUNTIME_CONFIG_EDITOR, &m_config->Runtime);
			}
			return;

		case ID_CONFIGURE_EDITOPCODECONFIGURATION:
			if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
				SetForegroundWindow(m_gameConfigEditor->Handle());
			else {
				if (m_gameConfigEditor)
					delete m_gameConfigEditor;
				m_gameConfigEditor = new ConfigWindow(IDS_WINDOW_OPCODE_CONFIG_EDITOR, &m_config->Game);
			}
			return;

		case ID_CONFIGURE_CHECKFORUPDATEDOPCODES:
			CheckUpdatedOpcodes(true);
			return;

		case ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP:
			config.CheckForUpdatedOpcodesOnStartup.Toggle();
			return;

		case ID_CONFIGURE_USEMORECPUTIME:
			config.UseMoreCpuTime.Toggle();
			return;

		case ID_CONFIGURE_LOCKFRAMERATE:
			m_cleanupFramerateLockDialog = Dialog::FramerateLockingDialog::Show(m_app, m_hWnd);
			return;

		case ID_CONFIGURE_SYNCHRONIZEPROCESSING:
			config.SynchronizeProcessing.Toggle();
			return;

		case ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT:
			config.Language = Language::SystemDefault;
			return;

		case ID_CONFIGURE_LANGUAGE_ENGLISH:
			config.Language = Language::English;
			return;

		case ID_CONFIGURE_LANGUAGE_KOREAN:
			config.Language = Language::Korean;
			return;

		case ID_CONFIGURE_LANGUAGE_JAPANESE:
			config.Language = Language::Japanese;
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
			config.AlwaysOnTop_XivAlexMainWindow.Toggle();
			return;

		case ID_VIEW_ALWAYSONTOPGAME:
			config.AlwaysOnTop_GameMainWindow.Toggle();
			return;

		case ID_VIEW_HIDEONMINIMIZE:
			config.HideOnMinimize_XivAlexMainWindow.Toggle();
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

// https://stackoverflow.com/a/39097160/1800296
static bool FileEquals(const std::filesystem::path& filename1, const std::filesystem::path& filename2) {
	const auto ReadBufSize = 65536;
	const auto file1 = Utils::Win32::Handle::FromCreateFile(filename1, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	const auto file2 = Utils::Win32::Handle::FromCreateFile(filename2, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	const auto size1 = file1.GetFileSize(), size2 = file2.GetFileSize();
	if (size1 != size2)
		return false;

	std::string buf1(ReadBufSize, 0);
	std::string buf2(ReadBufSize, 0);
	for (uint64_t ptr = 0; ptr < size1; ptr += ReadBufSize) {
		const auto read = static_cast<size_t>(std::min<uint64_t>(ReadBufSize, size1 - ptr));
		file1.Read(ptr, &buf1[0], read, Utils::Win32::Handle::PartialIoMode::AlwaysFull);
		file2.Read(ptr, &buf2[0], read, Utils::Win32::Handle::PartialIoMode::AlwaysFull);
		if (0 != memcmp(&buf1[0], &buf2[0], read))
			return false;
	}

	return true;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ImportFontConfig(const std::filesystem::path& path) {
	const auto targetDirectory = m_config->Init.ResolveConfigStorageDirectoryPath() / "FontConfig";
	auto targetFileName = targetDirectory / path.filename().replace_extension(".json");
	auto alreadyExists = false;
	if (exists(targetFileName)) {
		for (int i = 0; exists(targetFileName); i++) {
			if (FileEquals(path, targetFileName)) {
				alreadyExists = true;
				break;
			}
			targetFileName = targetDirectory / std::format(L"{}_{}.json", path.filename().replace_extension("").wstring(), i);
		}
	}

	if (!alreadyExists) {
		// Test file
		void(Utils::ParseJsonFromFile(path).get<Sqex::FontCsv::CreateConfig::FontCreateConfig>());

		create_directories(targetDirectory);
		if (!CopyFileW(path.c_str(), targetFileName.c_str(), TRUE))
			throw Utils::Win32::Error("CopyFileW");
	}

	m_config->Runtime.OverrideFontConfig = targetFileName;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ImportMusicImportConfig(const std::filesystem::path& path) {
	const auto targetDirectory = m_config->Init.ResolveConfigStorageDirectoryPath() / "MusicImportConfig";
	auto targetFileName = targetDirectory / path.filename().replace_extension(".json");
	auto alreadyExists = false;
	if (exists(targetFileName)) {
		for (int i = 0; exists(targetFileName); i++) {
			if (FileEquals(path, targetFileName)) {
				alreadyExists = true;
				break;
			}
			targetFileName = targetDirectory / std::format(L"{}_{}.json", path.filename().replace_extension("").wstring(), i);
		}
	}

	if (!alreadyExists) {
		// Test file
		void(Utils::ParseJsonFromFile(path).get<Sqex::Sound::MusicImportConfig>());

		create_directories(targetDirectory);
		if (!CopyFileW(path.c_str(), targetFileName.c_str(), TRUE))
			throw Utils::Win32::Error("CopyFileW");
	}

	auto arr = m_config->Runtime.MusicImportConfig.Value();
	while (true) {
		const auto it = std::ranges::find(arr, targetFileName);
		if (it != arr.end())
			arr.erase(it);
		else
			break;
	}
	arr.emplace(arr.begin(), std::move(targetFileName));
	m_config->Runtime.MusicImportConfig = arr;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::ImportExcelTransformConfig(const std::filesystem::path& path) {
	const auto targetDirectory = m_config->Init.ResolveConfigStorageDirectoryPath() / "ExcelTransformConfig";
	auto targetFileName = targetDirectory / path.filename().replace_extension(".json");
	auto alreadyExists = false;
	if (exists(targetFileName)) {
		for (int i = 0; exists(targetFileName); i++) {
			if (FileEquals(path, targetFileName)) {
				alreadyExists = true;
				break;
			}
			targetFileName = targetDirectory / std::format(L"{}_{}.json", path.filename().replace_extension("").wstring(), i);
		}
	}

	if (!alreadyExists) {
		// Test file
		void(Utils::ParseJsonFromFile(path).get<Misc::ExcelTransformConfig::Config>());

		create_directories(targetDirectory);
		if (!CopyFileW(path.c_str(), targetFileName.c_str(), TRUE))
			throw Utils::Win32::Error("CopyFileW");
	}

	auto arr{ m_config->Runtime.ExcelTransformConfigFiles.Value() };
	while (true) {
		const auto it = std::ranges::find(arr, targetFileName);
		if (it != arr.end())
			arr.erase(it);
		else
			break;
	}
	arr.emplace(arr.begin(), std::move(targetFileName));
	m_config->Runtime.ExcelTransformConfigFiles = arr;
}

void XivAlexander::Apps::MainApp::Window::MainWindow::AddAdditionalGameRootDirectory(std::filesystem::path path) {
	if (!exists(path / "ffxiv_dx11.exe") && !exists(path / "ffxiv.exe")) {
		Dll::MessageBoxF(m_hWnd, MB_ICONWARNING, m_config->Runtime.GetStringRes(IDS_ERROR_NOT_GAME_DIRECTORY), path.wstring());
		return;
	}

	auto arr{ m_config->Runtime.AdditionalSqpackRootDirectories.Value() };
	if (std::ranges::find(arr, path) != arr.end())
		return;
	while (true) {
		const auto it = std::ranges::find(arr, path);
		if (it != arr.end())
			arr.erase(it);
		else
			break;
	}
	arr.emplace(arr.begin(), std::move(path));
	m_config->Runtime.AdditionalSqpackRootDirectories = arr;
}

std::wstring CreateTtmpDirectoryName(const std::string& modPackName, std::wstring fileName) {
	auto name = Utils::FromUtf8(modPackName);
	if (name.empty())
		name = std::move(fileName);
	if (name.empty())
		name = L"Unnamed";
	else
		name = std::format(L"Mod_{}", name);
	name = Utils::StringTrim(name);
	for (auto& c : name) {
		if (c == '/' || c == '<' || c == '>' || c == ':' || c == '"' || c == '\\' || c == '|' || c == '?' || c == '*')
			c = '_';
	}
	return name;
}

std::string XivAlexander::Apps::MainApp::Window::MainWindow::InstallTTMP(const std::filesystem::path& path, Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
	const auto targetDirectory = m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods";
	if (path.empty())
		return "";

	std::filesystem::path temporaryTtmpDirectory;
	std::filesystem::path targetTtmpDirectory;
	const auto finalizer = Utils::CallOnDestruction([&]() {
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
			libzippp::ZipArchive* pArc = nullptr;
			Utils::Win32::FileMapping mapping;
			Utils::Win32::FileMapping::View mapView;

			if (auto oemPath = Utils::ToUtf8(path.wstring(), CP_OEMCP);
				Utils::FromUtf8(oemPath, CP_OEMCP) == path) {
				pArc = new libzippp::ZipArchive(oemPath);
				pArc->open();
			} else {
				mapping = Utils::Win32::FileMapping::Create(f);
				mapView = Utils::Win32::FileMapping::View::Create(mapping);
				pArc = libzippp::ZipArchive::fromBuffer(mapView.AsSpan<char>().data(), static_cast<uint32_t>(f.GetFileSize()));
			}
			const auto freeArc = Utils::CallOnDestruction([pArc]() {
				pArc->close();
				delete pArc;
				});

			bool mplFound = false, mpdFound = false;
			for (const auto& entry : pArc->getEntries()) {
				auto name = Utils::FromUtf8(entry.getName());
				while (!name.empty() && (name[0] == L'/' || name[0] == L'\\'))
					name = name.substr(1);
				if (name.empty())
					continue;

				CharLowerW(&name[0]);
				mplFound |= name == L"ttmpl.mpl";
				mpdFound |= name == L"ttmpd.mpd";
			}

			if (!mplFound || !mpdFound)
				throw std::runtime_error(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_ARCHIVE_MISSING_TTMP)));

			for (const auto& entry : pArc->getEntries()) {
				if (progressWindow.GetCancelEvent().Wait(0) == WAIT_OBJECT_0)
					return "";

				auto name = Utils::FromUtf8(entry.getName());
				while (!name.empty() && (name[0] == L'/' || name[0] == L'\\'))
					name = name.substr(1);
				if (name.empty())
					continue;

				std::ofstream o(temporaryTtmpDirectory / entry.getName(), std::ios::binary | std::ios::out);
				entry.readContent(o);
			}

			{
				const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream(temporaryTtmpDirectory / "TTMPL.mpl"));
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
				throw std::runtime_error(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_TTMPD_MISSING)));
			const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{ f });
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

	if (auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks())
		sqpacks->AddNewTtmp(targetTtmpDirectory / "TTMPL.mpl", true, progressWindow);

	return offerConfiguration ? Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_NOTIFY_TTMP_HAS_CONFIGURATION)) : "";
}

std::pair<std::filesystem::path, std::string> XivAlexander::Apps::MainApp::Window::MainWindow::InstallAnyFile(const std::filesystem::path& path, Apps::MainApp::Window::ProgressPopupWindow& progressWindow) {
	auto fileNameLower = path.filename().wstring();
	CharLowerW(&fileNameLower[0]);
	if (fileNameLower == L"ffxiv_dx11.exe" || fileNameLower == L"ffxiv.exe") {
		AddAdditionalGameRootDirectory(path.parent_path());
		return std::make_pair(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_ADDADDITIONALGAMEROOTDIRECTORY)));
	}
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
		return std::make_pair(path, msg.empty() ? Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_TTMP_INSTALLED)) : msg);
	}
	const auto fileSize = file.GetFileSize();
	if (fileSize > 1048576)
		throw std::runtime_error("File too big");

	if (const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{ file }); !ttmpl.SimpleModsList.empty() || !ttmpl.ModPackPages.empty()) {
		const auto msg = InstallTTMP(path, progressWindow);
		return std::make_pair(path, msg.empty() ? Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_TTMP_INSTALLED)) : msg);
	}

	// Test file
	const auto json = Utils::ParseJsonFromFile(path);
	auto isPossiblyExcelTransformConfig = false, isPossiblyFontConfig = false, isPossiblyMusicImportConfig = false;
	try {
		void(json.get<Misc::ExcelTransformConfig::Config>());
		isPossiblyExcelTransformConfig = true;
	} catch (...) {
		// pass
	}
	try {
		void(json.get<Sqex::FontCsv::CreateConfig::FontCreateConfig>());
		isPossiblyFontConfig = true;
	} catch (...) {
		// pass
	}
	try {
		void(json.get<Sqex::Sound::MusicImportConfig>());
		isPossiblyMusicImportConfig = true;
	} catch (...) {
		// pass
	}
	if (isPossiblyExcelTransformConfig) {
		ImportExcelTransformConfig(path);
		return std::make_pair(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_EXCELCONFIGFILE_INSTALLED)));
	} else if (isPossiblyFontConfig) {
		ImportFontConfig(path);
		return std::make_pair(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_FONTCONFIG_INSTALLED)));
	} else if (isPossiblyMusicImportConfig) {
		ImportMusicImportConfig(path);
		return std::make_pair(path, std::string("MusicImportConfig installed"));
	} else
		throw std::runtime_error(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE)));
}

void XivAlexander::Apps::MainApp::Window::MainWindow::InstallMultipleFiles(const std::vector<std::filesystem::path>& paths) {
	if (paths.empty())
		return;

	std::vector<std::pair<std::filesystem::path, std::string>> success;
	std::vector<std::pair<std::filesystem::path, std::string>> ignored;
	{
		ProgressPopupWindow progressWindow(m_hWnd);
		progressWindow.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_IMPORTING)));
		progressWindow.UpdateProgress(0, 0);
		const auto showAfter = GetTickCount64() + 500;

		const auto adderThread = Utils::Win32::Thread(L"DropFiles Handler", [&]() {
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
							ignored.emplace_back(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_NO_MATCHING_FILES)));
					} else {
						try {
							auto res = InstallAnyFile(path, progressWindow);
							if (res.second.empty())
								ignored.emplace_back(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE)));
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
		Dll::MessageBoxF(m_hWnd, MB_OK, Utils::FromUtf8(report));
	else
		Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.GetStringRes(IDS_APP_NAME), L"{}\n\n{}", m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE), Utils::FromUtf8(report));

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
	void(Utils::Win32::Thread(L"CheckUpdatedOpcodes", [this, showResultMessageBox]() {
		try {
			const auto releaseInfo = Misc::GameInstallationDetector::GetGameReleaseInfo();

			std::string prev;
			try {
				const auto prevFile = Utils::Win32::Handle::FromCreateFile(m_config->Game.GetConfigPath(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING);
				prev.resize(static_cast<size_t>(prevFile.GetFileSize()));
				prevFile.Read(0, &prev[0], prev.size());
				prev = nlohmann::json::parse(prev).dump();
			} catch (...) {
				prev.clear();
			}

			std::string updated;
			{
				std::stringstream out;
				curlpp::Easy req;
				req.setOpt(curlpp::options::Url(std::format("https://raw.githubusercontent.com/Soreepeong/XivAlexander/main/StaticData/OpcodeDefinition/game.{}.{}.json", releaseInfo.CountryCode, releaseInfo.GameVersion)));
				req.setOpt(curlpp::options::UserAgent("Mozilla/5.0"));
				req.setOpt(curlpp::options::FollowLocation(true));
				req.setOpt(curlpp::options::WriteStream(&out));

				if (WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyInfo{}; WinHttpGetIEProxyConfigForCurrentUser(&proxyInfo)) {
					std::wstring proxy;
					std::vector<std::wstring> proxyBypass;
					if (proxyInfo.lpszProxy) {
						proxy = proxyInfo.lpszProxy;
						GlobalFree(proxyInfo.lpszProxy);
					}
					if (proxyInfo.lpszProxyBypass) {
						proxyBypass = Utils::StringSplit<std::wstring>(Utils::StringReplaceAll<std::wstring>(proxyInfo.lpszProxyBypass, L";", L" "), L" ");
						GlobalFree(proxyInfo.lpszProxyBypass);
					}
					if (proxyInfo.lpszAutoConfigUrl)
						GlobalFree(proxyInfo.lpszAutoConfigUrl);
					bool noProxy = proxy.empty();
					for (const auto& v : proxyBypass) {
						if (lstrcmpiW(&v[0], L"raw.githubusercontent.com") == 0) {
							noProxy = true;
						}
					}
					if (!noProxy) {
						req.setOpt(curlpp::options::Proxy(Utils::ToUtf8(proxy)));
					}
				}

				req.perform();

				switch (int respCode = curlpp::infos::ResponseCode::get(req)) {
					case 200:
						updated = nlohmann::json::parse(out.str()).dump();
						break;

					case 404:
						m_logger->Log(LogCategory::General, "No updates to opcodes yet.");
						if (showResultMessageBox)
							Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_ERROR_404));
						return;

					default:
						throw std::runtime_error(std::format("HTTP Error {}", respCode));
				}
			}

			if (updated == prev) {
				m_logger->Log(LogCategory::General, "No updates to opcodes.");
				if (showResultMessageBox)
					Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_OK_NOTCHANGED));
				return;
			}

			std::ofstream(m_config->Game.GetConfigPath()) << updated;

			m_config->Game.Reload();

			m_logger->Log(LogCategory::General, "Opcodes updated.");
			if (showResultMessageBox)
				Dll::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.FormatStringRes(IDS_OPCODEUPDATE_OK_CHANGED));

		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Error>(LogCategory::General, "Opcode update check failed: {}", e.what());

			if (showResultMessageBox)
				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
		// TODO
		}));
}

void XivAlexander::Apps::MainApp::Window::MainWindow::BatchTtmpOperation(Internal::VirtualSqPacks::NestedTtmp& parent, int menuId) {
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
			if (auto& sqpacks = m_app.GetGameResourceOverrider().GetVirtualSqPacks()) {
				m_backgroundWorkerThread = Utils::Win32::Thread(L"BatchTtmpOperationOnOtherThread", [this, &parent, &sqpacks, menuId] {
					m_backgroundWorkerProgressWindow = std::make_shared<ProgressPopupWindow>(nullptr);
					const auto workerThread = Utils::Win32::Thread(L"BatchTtmpOperation", [&] {
						parent.Traverse(false, [&](Internal::VirtualSqPacks::NestedTtmp& nestedTtmp) {
							if (!nestedTtmp.Ttmp)
								return;

							auto& set = *nestedTtmp.Ttmp;
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
							return;
							});

						sqpacks->RescanTtmp(*m_backgroundWorkerProgressWindow);
						});

					do {
						m_backgroundWorkerProgressWindow->UpdateMessage(m_config->Runtime.GetStringRes(IDS_TITLE_DISCOVERINGFILES));
						m_backgroundWorkerProgressWindow->Show();
					} while (WAIT_TIMEOUT == m_backgroundWorkerProgressWindow->DoModalLoop(100, { workerThread }));
					workerThread.Wait();
					m_backgroundWorkerThread = nullptr;
					m_backgroundWorkerProgressWindow = nullptr;
					});
			}
		}
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}
