#include "pch.h"
#include "App_Window_MainWindow.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Sqex_CommandLine.h>
#include <XivAlexanderCommon/Sqex_FontCsv_CreateConfig.h>
#include <XivAlexanderCommon/Sqex_ThirdParty_TexTools.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_Feature_GameResourceOverrider.h"
#include "App_Misc_ExcelTransformConfig.h"
#include "App_Misc_Logger.h"
#include "App_Misc_VirtualSqPacks.h"
#include "App_Network_SocketHook.h"
#include "App_Window_ConfigWindow.h"
#include "App_Window_ProgressPopupWindow.h"
#include "App_XivAlexApp.h"
#include "DllMain.h"
#include "resource.h"

#define WM_COPYGLOBALDATA 0x0049

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
	wcex.lpszClassName = L"XivAlexander::Window::MainWindow";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::MainWindow::MainWindow(XivAlexApp* pApp, std::function<void()> unloadFunction)
	: BaseWindow(WindowClass(), nullptr, WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, 480, 160, nullptr, nullptr)
	, m_pApp(pApp)
	, m_triggerUnload(std::move(unloadFunction))
	, m_uTaskbarRestartMessage(RegisterWindowMessageW(L"TaskbarCreated"))
	, m_path(Utils::Win32::Process::Current().PathOf())
	, m_bUseElevation(Utils::Win32::IsUserAnAdmin())
	, m_launchParameters([this]() -> decltype(m_launchParameters) {
		try {
			return Sqex::CommandLine::FromString(Utils::ToUtf8(Utils::Win32::GetCommandLineWithoutProgramName(Dll::GetOriginalCommandLine())), &m_bUseParameterObfuscation);
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::General, m_config->Runtime.GetLangId(), IDS_WARNING_GAME_PARAMETER_PARSE, e.what());
			return {};
		}
	}()) {

	std::tie(m_sRegion, m_sVersion) = XivAlex::ResolveGameReleaseRegion();

	if (m_sRegion == L"JP" && !m_launchParameters.empty()) {
		m_gameLanguage = Sqex::Language::English;
		m_gameRegion = Sqex::Region::Japan;
		for (const auto& pair : m_launchParameters) {
			if (pair.first == "language")
				m_gameLanguage = static_cast<Sqex::Language>(1 + std::strtol(pair.second.c_str(), nullptr, 0));
			else if (pair.first == "SYS.Region")
				m_gameRegion = static_cast<Sqex::Region>(std::strtol(pair.second.c_str(), nullptr, 0));
		}
	} else if (m_sRegion == L"CN") {
		m_gameLanguage = Sqex::Language::ChineseSimplified;
		m_gameRegion = Sqex::Region::China;
	} else if (m_sRegion == L"KR") {
		m_gameLanguage = Sqex::Language::Korean;
		m_gameRegion = Sqex::Region::Korea;
	}

	RegisterTrayIcon();

	// Try to restore tray icon every 5 seconds in case things go wrong
	SetTimer(m_hWnd, TimerIdReregisterTrayIcon, 5000, nullptr);

	SetTimer(m_hWnd, TimerIdRepaint, 1000, nullptr);

	m_cleanup += m_config->Runtime.AlwaysOnTop_XivAlexMainWindow.OnChangeListener([this](auto&) {
		SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexMainWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	});

	m_cleanup += m_config->Runtime.ShowControlWindow.OnChangeListener([this](auto&) {
		ShowWindow(m_hWnd, m_config->Runtime.ShowControlWindow ? SW_SHOW : SW_HIDE);
		if (m_config->Runtime.ShowControlWindow)
			SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexMainWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	});
	if (m_config->Runtime.ShowControlWindow) {
		ShowWindow(m_hWnd, SW_SHOW);
		SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexMainWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	m_cleanup += m_config->Runtime.AdditionalSqpackRootDirectories.OnChangeListener([this](auto&) {
		RepopulateMenu();
	});
	m_cleanup += m_config->Runtime.ExcelTransformConfigFiles.OnChangeListener([this](auto&) {
		RepopulateMenu();
	});
	m_cleanup += m_config->Runtime.OverrideFontConfig.OnChangeListener([this](auto&) {
		RepopulateMenu();
	});

	if (auto overrider = Feature::GameResourceOverrider(); !overrider.CanUnload()) {
		if (const auto sqpacks = overrider.GetVirtualSqPacks())
			m_cleanup += sqpacks->OnTtmpSetsChanged([this]() { RepopulateMenu(); });
	}

	m_cleanup += m_pApp->GetSocketHook()->OnSocketFound([this](auto&) {
		InvalidateRect(m_hWnd, nullptr, false);
	});
	m_cleanup += m_pApp->GetSocketHook()->OnSocketGone([this](auto&) {
		InvalidateRect(m_hWnd, nullptr, false);
	});
	ApplyLanguage(m_config->Runtime.GetLangId());

	DragAcceptFiles(m_hWnd, TRUE);
	ChangeWindowMessageFilterEx(m_hWnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(m_hWnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(m_hWnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);
}

App::Window::MainWindow::~MainWindow() {
	m_cleanup.Clear();
	Destroy();
}

void App::Window::MainWindow::ShowContextMenu(const BaseWindow* parent) const {
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

void App::Window::MainWindow::ApplyLanguage(WORD languageId) {
	m_hAcceleratorWindow = {Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_TRAY_ACCELERATOR), languageId};
	m_hAcceleratorThread = {Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_TRAY_GLOBAL_ACCELERATOR), languageId};
	RepopulateMenu();

	const auto title = std::format(L"{}: {}, {}, {}",
		m_config->Runtime.GetStringRes(IDS_APP_NAME), GetCurrentProcessId(), m_sRegion, m_sVersion);
	SetWindowTextW(m_hWnd, title.c_str());
	InvalidateRect(m_hWnd, nullptr, FALSE);
}

LRESULT App::Window::MainWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_CLOSE) {
		if (lParam)
			DestroyWindow(m_hWnd);
		else {
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
				if (m_menuIdCallbacks.find(menuId) != m_menuIdCallbacks.end()) {
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
				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
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
				if ((m_config->Runtime.ShowControlWindow = !m_config->Runtime.ShowControlWindow))
					willShowControlWindow = true;
				m_lastTrayIconLeftButtonUp = 0;
			} else
				m_lastTrayIconLeftButtonUp = now;

			if (!willShowControlWindow) {
				if (const auto hWndGame = m_pApp->GetGameWindowHandle()) {
					const auto temporaryFocus = WithTemporaryFocus();
					SetForegroundWindow(hWndGame);
				}
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
		NONCLIENTMETRICSW ncm = {sizeof(NONCLIENTMETRICSW)};
		SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);

		GetClientRect(m_hWnd, &rect);
		const auto backdc = CreateCompatibleDC(hdc);

		std::vector<HGDIOBJ> gdiRestoreStack;
		gdiRestoreStack.emplace_back(SelectObject(backdc, CreateCompatibleBitmap(hdc, rect.right - rect.left, rect.bottom - rect.top)));
		gdiRestoreStack.emplace_back(SelectObject(backdc, CreateFontIndirectW(&ncm.lfMessageFont)));

		FillRect(backdc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		std::wstring str;
		try {
			str = m_config->Runtime.FormatStringRes(IDS_MAIN_TEXT,
				GetCurrentProcessId(), m_path, m_sVersion, m_sRegion,
				m_pApp->GetSocketHook()->Describe());
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
	}
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
}

void App::Window::MainWindow::OnDestroy() {
	m_triggerUnload();

	m_runtimeConfigEditor = nullptr;
	m_gameConfigEditor = nullptr;
	RemoveTrayIcon();
	BaseWindow::OnDestroy();
	PostQuitMessage(0);
}

void App::Window::MainWindow::RepopulateMenu() {
	if (GetWindowThreadProcessId(m_hWnd, nullptr) != GetCurrentThreadId()) {
		RunOnUiThread([this]() {
			RepopulateMenu();
		});
		return;
	}

	Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_TRAY_MENU), m_config->Runtime.GetLangId()).AttachAndSwap(m_hWnd);
	const auto hMenu = GetMenu(m_hWnd);
	const auto& config = m_config->Runtime;

	const auto title = std::format(L"{}: {}, {}, {}",
		m_config->Runtime.GetStringRes(IDS_APP_NAME), GetCurrentProcessId(), m_sRegion, m_sVersion);
	ModifyMenuW(hMenu, ID_FILE_CURRENTINFO, MF_BYCOMMAND | MF_DISABLED, ID_FILE_CURRENTINFO, title.c_str());

	m_menuIdCallbacks.clear();
	const auto allocateMenuId = [&](std::function<void()> cb) -> UINT_PTR {
		uint16_t counter = 50000;

		MENUITEMINFOW mii = {sizeof(MENUITEMINFOW)};
		mii.fMask = MIIM_STATE;

		while (m_menuIdCallbacks.find(counter) != m_menuIdCallbacks.end() || GetMenuItemInfoW(hMenu, counter, MF_BYCOMMAND, &mii)) {
			++counter;
		}
		m_menuIdCallbacks.emplace(counter, std::move(cb));
		return counter;
	};
	const auto getMenuText = [](HMENU parentMenu, UINT index) {
		std::wstring res(static_cast<size_t>(1) + GetMenuStringW(parentMenu, index, nullptr, 0, MF_BYPOSITION), '\0');
		GetMenuStringW(parentMenu, index, &res[0], static_cast<UINT>(res.size()), MF_BYPOSITION);
		return res;
	};
	{
		const auto currentConfig = m_config->TranslatePath(config.OverrideFontConfig.Value());
		const auto hParentMenu = GetSubMenu(GetSubMenu(hMenu, 3), 9);
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
				AppendMenuW(hParentMenu, MF_STRING | (eq ? MF_CHECKED : 0), allocateMenuId([this, path = entry.path()]() {
					m_config->Runtime.OverrideFontConfig = path;
				}), entry.path().filename().wstring().c_str());
				count++;
			}
		} catch (const std::filesystem::filesystem_error&) {
			// pass
		}
		if (!foundEq && !currentConfig.empty()) {
			AppendMenuW(hParentMenu, MF_STRING | MF_CHECKED, allocateMenuId([]() { /* do nothing */ }), currentConfig.wstring().c_str());
			count++;
		}
		if (count)
			DeleteMenu(hParentMenu, ID_MODDING_CHANGEFONT_NOENTRY, MF_BYCOMMAND);
		DeleteMenu(hParentMenu, ID_MODDING_CHANGEFONT_ENTRY, MF_BYCOMMAND);
	}
	{
		const auto hParentMenu = GetSubMenu(GetSubMenu(hMenu, 3), 10);
		auto count = 0;
		for (const auto& additionalRoot : config.AdditionalSqpackRootDirectories.Value()) {
			AppendMenuW(hParentMenu, MF_STRING, allocateMenuId([this, additionalRoot]() {
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
	}
	{
		const auto hParentMenu = GetSubMenu(GetSubMenu(hMenu, 3), 11);
		auto count = 0;
		for (const auto& file : config.ExcelTransformConfigFiles.Value()) {
			AppendMenuW(hParentMenu, MF_STRING, allocateMenuId([this, file]() {
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
			RemoveMenu(hParentMenu, ID_MODDING_EXDFTRANSFORMATIONRULES_NOENTRY, MF_BYCOMMAND);
		RemoveMenu(hParentMenu, ID_MODDING_EXDFTRANSFORMATIONRULES_ENTRY, MF_BYCOMMAND);
	}
	{
		const auto hParentMenu = GetSubMenu(GetSubMenu(hMenu, 3), 12);
		const auto hTemplateEntryMenu = GetSubMenu(hParentMenu, 0);
		RemoveMenu(hParentMenu, 0, MF_BYPOSITION);
		const auto deleteTemplateMenu = Utils::CallOnDestruction([hTemplateEntryMenu]() { DestroyMenu(hTemplateEntryMenu); });

		auto count = 0;

		if (auto overrider = Feature::GameResourceOverrider(); !overrider.CanUnload()) {

			if (const auto sqpacks = overrider.GetVirtualSqPacks()) {
				for (auto& ttmpSet : sqpacks->TtmpSets()) {
					const auto hSubMenu = CreatePopupMenu();
					AppendMenuW(hSubMenu, MF_STRING | (ttmpSet.Enabled ? MF_CHECKED : 0), allocateMenuId([this, &ttmpSet]() {
						try {
							ttmpSet.Enabled = !ttmpSet.Enabled;
							ttmpSet.ApplyChanges();
						} catch (const std::exception& e) {
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
						}
					}), getMenuText(hTemplateEntryMenu, 0).c_str());

					AppendMenuW(hSubMenu, MF_STRING, allocateMenuId([this, &ttmpSet, &sqpacks]() {
						try {
							if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.GetStringRes(IDS_APP_NAME),
								L"Delete \"{}\" at \"{}\"?", ttmpSet.List.Name, ttmpSet.ListPath.wstring()) == IDYES)
								sqpacks->DeleteTtmp(ttmpSet.ListPath);
						} catch (const std::exception& e) {
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
						}
					}), getMenuText(hTemplateEntryMenu, 1).c_str());

					if (!ttmpSet.Allocated) {
						AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);
						AppendMenuW(hSubMenu, MF_STRING | MF_DISABLED, 0, getMenuText(hTemplateEntryMenu, 3).c_str());
					}

					if (!ttmpSet.List.ModPackPages.empty())
						AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);

					for (size_t pageObjectIndex = 0; pageObjectIndex < ttmpSet.List.ModPackPages.size(); ++pageObjectIndex) {
						const auto& modGroups = ttmpSet.List.ModPackPages[pageObjectIndex].ModGroups;
						if (modGroups.empty())
							continue;
						const auto pageConf = ttmpSet.Choices.at(pageObjectIndex);

						for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
							const auto& modGroup = modGroups[modGroupIndex];
							if (modGroup.OptionList.empty())
								continue;

							const auto choice = pageConf.at(modGroupIndex).get<size_t>();

							const auto hModSubMenu = CreatePopupMenu();
							for (size_t optionIndex = 0; optionIndex < modGroup.OptionList.size(); ++optionIndex) {
								const auto& modEntry = modGroup.OptionList[optionIndex];

								std::string description = modEntry.Name.empty() ? "-" : modEntry.Name;
								if (!modEntry.GroupName.empty())
									description += std::format(" ({})", modEntry.GroupName);
								if (!modEntry.Description.empty())
									description += std::format(" ({})", modEntry.Description);

								AppendMenuW(hModSubMenu, MF_STRING | (optionIndex == choice ? MF_CHECKED : 0), allocateMenuId(
									[this, pageObjectIndex, modGroupIndex, optionIndex, &ttmpSet]() {
										try {
											if (!ttmpSet.Choices.is_array())
												ttmpSet.Choices = nlohmann::json::array();
											while (ttmpSet.Choices.size() <= pageObjectIndex)
												ttmpSet.Choices.insert(ttmpSet.Choices.end(), nlohmann::json::array());

											auto& page = ttmpSet.Choices.at(pageObjectIndex);
											if (!page.is_array())
												page = nlohmann::json::array();
											while (page.size() <= modGroupIndex)
												page.insert(page.end(), 0);
											page[modGroupIndex] = optionIndex;

											ttmpSet.ApplyChanges();

										} catch (const std::exception& e) {
											Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
										}
									}), Utils::FromUtf8(description).c_str());
							}
							AppendMenuW(hSubMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hModSubMenu), Utils::FromUtf8(modGroup.GroupName).c_str());
						}
					}

					AppendMenuW(hParentMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), ttmpSet.ListPath.parent_path().filename().wstring().c_str());
					count++;
				}
			}
		}

		//try {
		//	for (const auto& entry : std::filesystem::recursive_directory_iterator(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods")) {
		//		const auto choicesPath = entry.path().parent_path() / "choices.json";
		//		auto lower = entry.path().filename().wstring();
		//		CharLowerW(&lower[0]);
		//		if (lower != L"ttmpl.mpl" || !exists(entry.path().parent_path() / L"TTMPD.mpd"))
		//			continue;
		//
		//		const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream(entry.path()));
		//		auto conf = nlohmann::json::object();
		//		try {
		//			conf = Utils::ParseJsonFromFile(choicesPath);
		//		} catch (...) {
		//			// pass
		//		}
		//
		//		const auto disableFilePath = entry.path().parent_path() / "disable";
		//		const auto bDisabled = exists(disableFilePath);
		//		const auto deleteFilePath = entry.path().parent_path() / "delete";
		//		const auto bDelete = exists(deleteFilePath);
		//
		//		const auto hSubMenu = CreatePopupMenu();
		//		AppendMenuW(hSubMenu, MF_STRING | (bDisabled ? 0 : MF_CHECKED), allocateMenuId([this, disableFilePath]() {
		//			try {
		//				if (exists(disableFilePath))
		//					remove(disableFilePath);
		//				else
		//					Utils::Win32::File::Create(disableFilePath, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0);
		//			} catch (const std::exception& e) {
		//				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR,
		//					m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
		//				return;
		//			}
		//			RepopulateMenu();
		//		}), getMenuText(hTemplateEntryMenu, 0).c_str());
		//
		//		AppendMenuW(hSubMenu, MF_STRING | (bDelete ? MF_CHECKED : 0), allocateMenuId([this, deleteFilePath]() {
		//			try {
		//				if (exists(deleteFilePath))
		//					remove(deleteFilePath);
		//				else
		//					Utils::Win32::File::Create(deleteFilePath, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0);
		//			} catch (const std::exception& e) {
		//				Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR,
		//					m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
		//				return;
		//			}
		//			RepopulateMenu();
		//		}), getMenuText(hTemplateEntryMenu, 1).c_str());
		//
		//		AppendMenuW(hSubMenu, MF_SEPARATOR, 0, nullptr);
		//
		//		if (!ttmpl.SimpleModsList.empty()) {
		//			const auto hModSubMenu = CreatePopupMenu();
		//			for (size_t i = 0; i < ttmpl.SimpleModsList.size(); ++i) {
		//				const auto& modEntry = ttmpl.SimpleModsList[i];
		//				const auto entryDisabled = conf.is_array() && i < conf.size() && conf[i].is_boolean() && !conf[i].get<boolean>();
		//				AppendMenuW(hModSubMenu, MF_STRING | (entryDisabled ? 0 : MF_CHECKED), allocateMenuId(
		//						[this, i, entryDisabled, choicesPath, prevConf = conf]() {
		//							try {
		//								auto conf = prevConf;
		//								if (!conf.is_array())
		//									conf = nlohmann::json::array();
		//								while (conf.size() <= i)
		//									conf.insert(conf.end(), true);
		//								conf[i] = entryDisabled;
		//
		//								Utils::SaveJsonToFile(choicesPath, conf);
		//
		//							} catch (const std::exception& e) {
		//								Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR,
		//									m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
		//								return;
		//							}
		//							RepopulateMenu();
		//						}),
		//					std::format(L"{} ({})",
		//						modEntry.Name.empty() ? "-" : modEntry.Name,
		//						modEntry.Category.empty() ? "-" : modEntry.Category).c_str());
		//			}
		//			AppendMenuW(hSubMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hModSubMenu), m_config->Runtime.GetStringRes(IDS_CONFIGURE));
		//		}
		//
		//		for (size_t pageObjectIndex = 0; pageObjectIndex < ttmpl.ModPackPages.size(); ++pageObjectIndex) {
		//			const auto& modGroups = ttmpl.ModPackPages[pageObjectIndex].ModGroups;
		//			if (modGroups.empty())
		//				continue;
		//			const auto pageConf = conf.is_array() && pageObjectIndex < conf.size() && conf[pageObjectIndex].is_array() ? conf[pageObjectIndex] : nlohmann::json::array();
		//
		//			for (size_t modGroupIndex = 0; modGroupIndex < modGroups.size(); ++modGroupIndex) {
		//				const auto& modGroup = modGroups[modGroupIndex];
		//				if (modGroup.OptionList.empty())
		//					continue;
		//
		//				const auto choice = static_cast<size_t>(modGroupIndex < pageConf.size() ? std::max(0, std::min(static_cast<int>(modGroup.OptionList.size() - 1), pageConf[modGroupIndex].get<int>())) : 0);
		//				const auto hModSubMenu = CreatePopupMenu();
		//				for (size_t optionIndex = 0; optionIndex < modGroup.OptionList.size(); ++optionIndex) {
		//					const auto& modEntry = modGroup.OptionList[optionIndex];
		//
		//					std::string description = modEntry.Name.empty() ? "-" : modEntry.Name;
		//					if (!modEntry.GroupName.empty())
		//						description += std::format(" ({})", modEntry.GroupName);
		//					if (!modEntry.Description.empty())
		//						description += std::format(" ({})", modEntry.Description);
		//
		//					AppendMenuW(hModSubMenu, MF_STRING | (optionIndex == choice ? MF_CHECKED : 0), allocateMenuId(
		//						[this, pageObjectIndex, modGroupIndex, optionIndex, choicesPath, prevConf = conf]() {
		//							try {
		//								auto conf = prevConf;
		//								if (!conf.is_array())
		//									conf = nlohmann::json::array();
		//								while (conf.size() <= pageObjectIndex)
		//									conf.insert(conf.end(), nlohmann::json::array());
		//
		//								auto& page = conf.at(pageObjectIndex);
		//								if (!page.is_array())
		//									page = nlohmann::json::array();
		//								while (page.size() <= modGroupIndex)
		//									page.insert(page.end(), 0);
		//								page[modGroupIndex] = optionIndex;
		//
		//								Utils::SaveJsonToFile(choicesPath, conf);
		//
		//							} catch (const std::exception& e) {
		//								Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR,
		//									m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
		//								return;
		//							}
		//							RepopulateMenu();
		//						}), Utils::FromUtf8(description).c_str());
		//				}
		//				AppendMenuW(hSubMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hModSubMenu), Utils::FromUtf8(modGroup.GroupName).c_str());
		//			}
		//		}
		//
		//		AppendMenuW(hParentMenu, MF_STRING | MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), entry.path().parent_path().filename().wstring().c_str());
		//		count++;
		//	}
		//} catch (const std::filesystem::filesystem_error&) {
		//	// pass
		//}
		if (count)
			DeleteMenu(hParentMenu, ID_MODDING_TTMP_NOENTRY, MF_BYCOMMAND);
	}
}

void App::Window::MainWindow::SetMenuStates() const {
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
		const auto languageRegionModifiable = LanguageRegionModifiable();
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_ENGLISH, m_gameLanguage == Sqex::Language::English, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_GERMAN, m_gameLanguage == Sqex::Language::German, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_FRENCH, m_gameLanguage == Sqex::Language::French, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_JAPANESE, m_gameLanguage == Sqex::Language::Japanese, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE, m_gameLanguage == Sqex::Language::ChineseSimplified, false);
		SetMenuState(hMenu, ID_RESTART_LANGUAGE_KOREAN, m_gameLanguage == Sqex::Language::Korean, false);
		SetMenuState(hMenu, ID_RESTART_REGION_JAPAN, m_gameRegion == Sqex::Region::Japan, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_REGION_NORTH_AMERICA, m_gameRegion == Sqex::Region::NorthAmerica, languageRegionModifiable);
		SetMenuState(hMenu, ID_RESTART_REGION_EUROPE, m_gameRegion == Sqex::Region::Europe, languageRegionModifiable);
	}

	// Network
	{
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE, config.UseHighLatencyMitigation, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1, config.HighLatencyMitigationMode == HighLatencyMitigationMode::SubtractLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_2, config.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateRtt, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3, config.HighLatencyMitigationMode == HighLatencyMitigationMode::SimulateNormalizedRttAndLatency, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_USEEARLYPENALTY, config.UseEarlyPenalty, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING, config.UseHighLatencyMitigationLogging, true);
		SetMenuState(hMenu, ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE, config.UseHighLatencyMitigationPreviewMode, true);
		SetMenuState(hMenu, ID_NETWORK_USEIPCTYPEFINDER, config.UseOpcodeFinder, true);
		SetMenuState(hMenu, ID_NETWORK_USEALLIPCMESSAGELOGGER, config.UseAllIpcMessageLogger, true);
		SetMenuState(hMenu, ID_NETWORK_LOGEFFECTAPPLICATIONDELAY, config.UseEffectApplicationDelayLogger, true);
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

		SetMenuState(hMenu, ID_MODDING_CHANGEFONT_DISABLE, config.OverrideFontConfig.Value().empty(), true);
	}

	// Configure
	{
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT, config.Language == Language::SystemDefault, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_ENGLISH, config.Language == Language::English, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_KOREAN, config.Language == Language::Korean, true);
		SetMenuState(hMenu, ID_CONFIGURE_LANGUAGE_JAPANESE, config.Language == Language::Japanese, true);
	}

	// View
	SetMenuState(hMenu, ID_VIEW_ALWAYSONTOP, config.AlwaysOnTop_XivAlexMainWindow, true);
	SetMenuState(hMenu, ID_VIEW_ALWAYSONTOPGAME, config.AlwaysOnTop_GameMainWindow, true);
}

void App::Window::MainWindow::RegisterTrayIcon() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"LoadIconW");
	NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
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

void App::Window::MainWindow::RemoveTrayIcon() {
	NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
	nid.uID = TrayItemId;
	nid.hWnd = m_hWnd;
	nid.uFlags = NIF_GUID;
	Shell_NotifyIconW(NIM_DELETE, &nid);
}

bool App::Window::MainWindow::LanguageRegionModifiable() const {
	return m_gameRegion == Sqex::Region::Japan
		|| m_gameRegion == Sqex::Region::NorthAmerica
		|| m_gameRegion == Sqex::Region::Europe;
}

void App::Window::MainWindow::AskRestartGame(bool onlyOnModifier) {
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
		const auto process = Utils::Win32::Process::Current();
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
		const auto revertInjectOnCreateProcess = Utils::CallOnDestruction([]() { XivAlexDll::EnableInjectOnCreateProcess(XivAlexDll::InjectOnCreateProcessAppFlags::Use | XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly); });
		bool ok;

		// This only mattered on initialization at AutoLoadAsDependencyModule, so it's safe to modify and not revert
		if (m_bUseXivAlexander)
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);
		else
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", L"1");

		if (!Dll::IsLoadedAsDependency() && m_bUseXivAlexander)
			ok = Utils::Win32::RunProgram({
				.path = Dll::Module().PathOf().parent_path() / (m_bUseDirectX11 ? XivAlex::XivAlexLoader64NameW : XivAlex::XivAlexLoader32NameW),
				.args = std::format(L"-a launcher -l select \"{}\" {}", game, Sqex::CommandLine::ToString(params, m_bUseParameterObfuscation)),
				.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
			});
		else
			ok = Utils::Win32::RunProgram({
				.path = game,
				.args = Utils::FromUtf8(Sqex::CommandLine::ToString(params, m_bUseParameterObfuscation)),
				.elevateMode = m_bUseElevation ? Utils::Win32::RunProgramParams::Force : Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated,
			});

		if (ok) {
			RemoveTrayIcon();
			process.Terminate(0);
		}
	}
}

void App::Window::MainWindow::OnCommand_Menu_File(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_GLOBAL_SHOW_TRAYMENU:
			for (const auto& w : All())
				if (w->Handle() == GetForegroundWindow())
					ShowContextMenu(w);
			return;

		case ID_FILE_SHOWLOGGINGWINDOW:
			config.ShowLoggingWindow = !config.ShowLoggingWindow;
			return;

		case ID_FILE_SHOWCONTROLWINDOW:
			config.ShowControlWindow = !config.ShowControlWindow;
			SetForegroundWindow(m_hWnd);
			return;

		case ID_FILE_CHECKFORUPDATES:
			LaunchXivAlexLoaderWithTargetHandles({Utils::Win32::Process::Current()},
				Dll::IsLoadedAsDependency() ? XivAlexDll::LoaderAction::Internal_Update_DependencyDllMode : XivAlexDll::LoaderAction::UpdateCheck,
				false);
			return;

		case ID_FILE_UNLOADXIVALEXANDER:
			m_triggerUnload();
			return;

		case ID_FILE_FORCEEXITGAME:
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_CONFIRM_EXIT_GAME)) == IDYES) {
				RemoveTrayIcon();
				ExitProcess(0);
			}
			return;
	}
}

void App::Window::MainWindow::OnCommand_Menu_Restart(int menuId) {
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

		case ID_RESTART_LANGUAGE_ENGLISH:
			m_gameLanguage = Sqex::Language::English;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_GERMAN:
			m_gameLanguage = Sqex::Language::German;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_FRENCH:
			m_gameLanguage = Sqex::Language::French;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_JAPANESE:
			m_gameLanguage = Sqex::Language::Japanese;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE:
			m_gameLanguage = Sqex::Language::ChineseSimplified;
			AskRestartGame(true);
			return;

		case ID_RESTART_LANGUAGE_KOREAN:
			m_gameLanguage = Sqex::Language::Korean;
			AskRestartGame(true);
			return;

		case ID_RESTART_REGION_JAPAN:
			m_gameRegion = Sqex::Region::Japan;
			AskRestartGame(true);
			return;

		case ID_RESTART_REGION_NORTH_AMERICA:
			m_gameRegion = Sqex::Region::NorthAmerica;
			AskRestartGame(true);
			return;

		case ID_RESTART_REGION_EUROPE:
			m_gameRegion = Sqex::Region::Europe;
			AskRestartGame(true);
			return;
	}
}

void App::Window::MainWindow::OnCommand_Menu_Network(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE:
			config.UseHighLatencyMitigation = !config.UseHighLatencyMitigation;
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

		case ID_NETWORK_HIGHLATENCYMITIGATION_USEEARLYPENALTY:
			config.UseEarlyPenalty = !config.UseEarlyPenalty;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING:
			config.UseHighLatencyMitigationLogging = !config.UseHighLatencyMitigationLogging;
			return;

		case ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE:
			config.UseHighLatencyMitigationPreviewMode = !config.UseHighLatencyMitigationPreviewMode;
			return;

		case ID_NETWORK_USEALLIPCMESSAGELOGGER:
			config.UseAllIpcMessageLogger = !config.UseAllIpcMessageLogger;
			return;

		case ID_NETWORK_USEIPCTYPEFINDER:
			config.UseOpcodeFinder = !config.UseOpcodeFinder;
			return;

		case ID_NETWORK_LOGEFFECTAPPLICATIONDELAY:
			config.UseEffectApplicationDelayLogger = !config.UseEffectApplicationDelayLogger;
			return;

		case ID_NETWORK_REDUCEPACKETDELAY:
			config.ReducePacketDelay = !config.ReducePacketDelay;
			return;

		case ID_NETWORK_RELEASEALLCONNECTIONS:
			m_pApp->RunOnGameLoop([this]() {
				m_pApp->GetSocketHook()->ReleaseSockets();
			});
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES:
			config.TakeOverLoopbackAddresses = !config.TakeOverLoopbackAddresses;
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES:
			config.TakeOverPrivateAddresses = !config.TakeOverPrivateAddresses;
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES:
			config.TakeOverAllAddresses = !config.TakeOverAllAddresses;
			return;

		case ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS:
			config.TakeOverAllPorts = !config.TakeOverAllPorts;
			return;
	}
}

void App::Window::MainWindow::OnCommand_Menu_Modding(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_MODDING_ENABLE:
			config.UseModding = !config.UseModding;
			return;

		case ID_MODDING_LOGALLHASHKEYS:
			config.UseHashTrackerKeyLogging = !config.UseHashTrackerKeyLogging;
			return;

		case ID_MODDING_LOGALLFILEACCESS:
			config.LogAllDataFileRead = !config.LogAllDataFileRead;
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
				{m_config->Runtime.GetStringRes(IDS_FILTERSPEC_FONTPRESETJSON), L"*.json"},
				{m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*"},
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

		case ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_ADD:
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
					Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
				}
			}
			return;

		case ID_MODDING_ADDITIONALGAMEROOTDIRECTORIES_REMOVEALL:
			if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, "Unregister all additional game root directories?") == IDYES) {
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
				{m_config->Runtime.GetStringRes(IDS_FILTERSPEC_EXCELTRANSFORMCONFIGJSON), L"*.json"},
				{m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*"},
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
			if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, "Unregister all excel transformation rules?") == IDYES) {
				m_config->Runtime.ExcelTransformConfigFiles = std::vector<std::filesystem::path>();
			}
			return;

		case ID_MODDING_TTMP_IMPORT: {
			static const COMDLG_FILTERSPEC fileTypes[] = {
				{m_config->Runtime.GetStringRes(IDS_FILTERSPEC_TTMP), L"*.ttmp; *.ttmp2; *.mpl"},
				{m_config->Runtime.GetStringRes(IDS_FILTERSPEC_ALLFILES), L"*"},
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
							resultMessage = InstallTTMP(paths[0], progress.GetCancelEvent());
						} catch (const std::exception& e) {
							progress.Cancel();
							Dll::MessageBoxF(m_hWnd, MB_OK | MB_ICONERROR, m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()));
						}
					});

					while (WAIT_TIMEOUT == progress.DoModalLoop(100, {adderThread})) {
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
			std::string msg;
			switch (menuId) {
				case ID_MODDING_TTMP_REMOVEALL:
					msg = Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_CONFIRM_REMOVEALLTTMP));
					break;

				case ID_MODDING_TTMP_ENABLEALL:
					msg = Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_CONFIRM_ENABLEALLTTMP));
					break;

				case ID_MODDING_TTMP_DISABLEALL:
					msg = Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_CONFIRM_DISABLEALLTTMP));
					break;
			}
			if (Dll::MessageBoxF(m_hWnd, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, msg) == IDYES) {

				for (const auto& entry : std::filesystem::recursive_directory_iterator(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods")) {
					const auto choicesPath = entry.path().parent_path() / "choices.json";
					auto lower = entry.path().filename().wstring();
					CharLowerW(&lower[0]);
					if (lower != L"ttmpl.mpl" || !exists(entry.path().parent_path() / L"TTMPD.mpd"))
						continue;

					const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream(entry.path()));

					const auto deleteFilePath = entry.path().parent_path() / "delete";
					const auto disableFilePath = entry.path().parent_path() / "disable";

					if (auto overrider = Feature::GameResourceOverrider(); !overrider.CanUnload()) {
						if (const auto sqpacks = overrider.GetVirtualSqPacks()) {
							for (auto& set : sqpacks->TtmpSets()) {
								switch (menuId) {
									case ID_MODDING_TTMP_REMOVEALL:
										sqpacks->DeleteTtmp(set.ListPath);
										break;

									case ID_MODDING_TTMP_ENABLEALL:
										set.Enabled = true;
										set.ApplyChanges(false);
										break;

									case ID_MODDING_TTMP_DISABLEALL:
										set.Enabled = false;
										set.ApplyChanges(false);
										break;
								}
							}
							sqpacks->RescanTtmp();
						}
					}
				}
				RepopulateMenu();
			}
			return;
		}

		case ID_MODDING_TTMP_OPENDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath() / "TexToolsMods");
			return;

		case ID_MODDING_TTMP_REFRESH:
			if (auto overrider = Feature::GameResourceOverrider(); !overrider.CanUnload()) {
				if (const auto sqpacks = overrider.GetVirtualSqPacks())
					sqpacks->RescanTtmp();
			}
			return;

		case ID_MODDING_OPENREPLACEMENTFILEENTRIESDIRECTORY:
			EnsureAndOpenDirectory(m_config->Init.ResolveConfigStorageDirectoryPath() / "ReplacementFileEntries");
			return;
	}
}

void App::Window::MainWindow::OnCommand_Menu_Configure(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_CONFIGURE_EDITRUNTIMECONFIGURATION:
			if (m_runtimeConfigEditor && !m_runtimeConfigEditor->IsDestroyed())
				SetForegroundWindow(m_runtimeConfigEditor->Handle());
			else
				m_runtimeConfigEditor = std::make_unique<ConfigWindow>(IDS_WINDOW_RUNTIME_CONFIG_EDITOR, &m_config->Runtime);
			return;

		case ID_CONFIGURE_EDITOPCODECONFIGURATION:
			if (m_gameConfigEditor && !m_gameConfigEditor->IsDestroyed())
				SetForegroundWindow(m_gameConfigEditor->Handle());
			else
				m_gameConfigEditor = std::make_unique<ConfigWindow>(IDS_WINDOW_OPCODE_CONFIG_EDITOR, &m_config->Game);
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
			config.Reload({});
			return;
	}
}

void App::Window::MainWindow::OnCommand_Menu_View(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_VIEW_ALWAYSONTOP: {
			m_config->Runtime.AlwaysOnTop_XivAlexMainWindow = !m_config->Runtime.AlwaysOnTop_XivAlexMainWindow;
			return;
		}

		case ID_VIEW_ALWAYSONTOPGAME:
			config.AlwaysOnTop_GameMainWindow = !config.AlwaysOnTop_GameMainWindow;
			return;
	}
}

void App::Window::MainWindow::OnCommand_Menu_Help(int menuId) {
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

std::vector<std::filesystem::path> App::Window::MainWindow::ChooseFileToOpen(std::span<const COMDLG_FILTERSPEC> fileTypes, UINT nTitleResId, const std::filesystem::path& defaultPath) const {
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
	const auto file1 = Utils::Win32::File::Create(filename1, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	const auto file2 = Utils::Win32::File::Create(filename1, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	const auto size1 = file1.GetLength(), size2 = file2.GetLength();
	if (size1 != size2)
		return false;

	std::string buf1(ReadBufSize, 0);
	std::string buf2(ReadBufSize, 0);
	for (uint64_t ptr = 0; ptr < size1; ptr += ReadBufSize) {
		const auto read = static_cast<size_t>(std::min<uint64_t>(ReadBufSize, size1 - ptr));
		file1.Read(ptr, &buf1[0], read);
		file2.Read(ptr, &buf2[0], read);
		if (buf1 != buf2)
			return false;
	}

	return true;
}

void App::Window::MainWindow::ImportFontConfig(const std::filesystem::path& path) {
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

void App::Window::MainWindow::ImportExcelTransformConfig(const std::filesystem::path& path) {
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

	auto arr = m_config->Runtime.ExcelTransformConfigFiles.Value();
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

void App::Window::MainWindow::AddAdditionalGameRootDirectory(std::filesystem::path path) {
	if (!exists(path / "ffxiv_dx11.exe")
		|| !exists(path / "ffxiv.exe")) {
		Dll::MessageBoxF(m_hWnd, MB_ICONWARNING, m_config->Runtime.GetStringRes(IDS_ERROR_NOT_GAME_DIRECTORY), path.wstring());
		return;
	}

	auto arr = m_config->Runtime.AdditionalSqpackRootDirectories.Value();
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

std::string App::Window::MainWindow::InstallTTMP(const std::filesystem::path& path, const Utils::Win32::Event& cancelEvent) {
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
		const auto f = Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0);
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
				pArc = libzippp::ZipArchive::fromBuffer(mapView.AsSpan<char>().data(), static_cast<uint32_t>(f.GetLength()));
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
				if (cancelEvent.Wait(0) == WAIT_OBJECT_0)
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
			const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{f});
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

	if (auto overrider = Feature::GameResourceOverrider(); !overrider.CanUnload()) {
		if (const auto sqpacks = overrider.GetVirtualSqPacks())
			sqpacks->AddNewTtmp(targetTtmpDirectory / "TTMPL.mpl");
	}

	return offerConfiguration ? Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_NOTIFY_TTMP_HAS_CONFIGURATION)) : "";
}

std::pair<std::filesystem::path, std::string> App::Window::MainWindow::InstallAnyFile(const std::filesystem::path& path, const Utils::Win32::Event& cancelEvent) {
	auto fileNameLower = path.filename().wstring();
	CharLowerW(&fileNameLower[0]);
	if (fileNameLower == L"ffxiv_dx11.exe" || fileNameLower == L"ffxiv.exe") {
		AddAdditionalGameRootDirectory(path.parent_path());
		return std::make_pair(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_ADDADDITIONALGAMEROOTDIRECTORY)));
	}
	if (fileNameLower == L"ttmpd.mpd" || fileNameLower == L"choices.json")
		return {path, {}};
	if (!fileNameLower.ends_with(L".json")
		&& !fileNameLower.ends_with(L".mpl")
		&& !fileNameLower.ends_with(L".ttmp")
		&& !fileNameLower.ends_with(L".ttmp2"))
		return {path, {}};

	const auto file = Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);
	char buf[2];
	file.Read(0, buf, 2);
	if (buf[0] == 'P' && buf[1] == 'K') {
		const auto msg = InstallTTMP(path, cancelEvent);
		return std::make_pair(path, msg.empty() ? Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_TTMP_INSTALLED)) : msg);
	}
	const auto fileSize = file.GetLength();
	if (fileSize > 1048576)
		throw std::runtime_error("File too big");

	if (const auto ttmpl = Sqex::ThirdParty::TexTools::TTMPL::FromStream(Sqex::FileRandomAccessStream{file}); !ttmpl.SimpleModsList.empty() || !ttmpl.ModPackPages.empty()) {
		const auto msg = InstallTTMP(path, cancelEvent);
		return std::make_pair(path, msg.empty() ? Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_TTMP_INSTALLED)) : msg);
	}

	// Test file
	const auto json = Utils::ParseJsonFromFile(path);
	auto isPossiblyExcelTransformConfig = false, isPossiblyFontConfig = false;
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
	if (isPossiblyExcelTransformConfig) {
		ImportExcelTransformConfig(path);
		return std::make_pair(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_EXCELCONFIGFILE_INSTALLED)));
	} else if (isPossiblyFontConfig) {
		ImportFontConfig(path);
		return std::make_pair(path, Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_RESULT_FONTCONFIG_INSTALLED)));
	} else
		throw std::runtime_error(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_UNSUPPORTED_FILE_TYPE)));
}

void App::Window::MainWindow::InstallMultipleFiles(const std::vector<std::filesystem::path>& paths) {
	if (paths.empty())
		return;

	std::vector<std::pair<std::filesystem::path, std::string>> success;
	std::vector<std::pair<std::filesystem::path, std::string>> ignored;
	{
		ProgressPopupWindow progress(m_hWnd);
		progress.UpdateMessage(Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_TITLE_IMPORTING)));
		progress.UpdateProgress(0, 0);
		const auto showAfter = GetTickCount64() + 500;

		const auto adderThread = Utils::Win32::Thread(L"DropFiles Handler", [&]() {
			for (const auto& path : paths) {
				try {
					if (is_directory(path)) {
						auto anyAdded = false;
						for (const auto& item : std::filesystem::recursive_directory_iterator(path)) {
							if (!item.is_directory()) {
								try {
									auto res = InstallAnyFile(item.path(), progress.GetCancelEvent());
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
							auto res = InstallAnyFile(path, progress.GetCancelEvent());
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

		while (WAIT_TIMEOUT == progress.DoModalLoop(100, {adderThread})) {
			if (showAfter < GetTickCount64())
				progress.Show();
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

void App::Window::MainWindow::EnsureAndOpenDirectory(const std::filesystem::path& path) {
	if (!exists(path))
		create_directories(path);

	SHELLEXECUTEINFOW se{
		.cbSize = sizeof SHELLEXECUTEINFOW,
		.hwnd = m_hWnd,
		.lpVerb = L"explore",
		.lpFile = path.c_str(),
		.nShow = SW_SHOW,
	};
	if (!ShellExecuteExW(&se))
		throw Utils::Win32::Error("ShellExecuteExW");
}
