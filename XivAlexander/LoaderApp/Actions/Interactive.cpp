#include "pch.h"
#include "Interactive.h"

#include "Config.h"
#include "LoaderApp/Actions/InstallUninstall.h"
#include "LoaderApp/Actions/LoadUnload.h"
#include "LoaderApp/Actions/Update.h"
#include "LoaderApp/App.h"
#include "LoaderApp/LeftoverCleanup.h"
#include "LoaderApp/LoaderSettings.h"
#include "LoaderApp/LoaderUi.h"
#include "MainApp/Windows/GridView.h"
#include "MainApp/Windows/ThemeColors.h"
#include "Misc/GameInstallationDetector.h"
#include "resource.h"
#include "Utils/StringUtils.h"
#include "XivAlexander.h"

using namespace Dll;

namespace {
	namespace Detector = XivAlexander::Misc::GameInstallationDetector;
	namespace Window = XivAlexander::Apps::MainApp::Window;
	namespace Ui = XivAlexander::LoaderApp::Ui;
	namespace LeftoverCleanup = XivAlexander::LoaderApp::LeftoverCleanup;
	using XivAlexander::LoaderApp::Actions::InstallUninstall;
	using XivAlexander::LoaderApp::InstallMode;
	using XivAlexander::LoaderApp::LoaderSettings;
	using Window::GridView;

	enum : UINT {
		WmDiscovered = WM_APP + 1,
		WmDiscoveryDone,
		WmOperationDone,
		WmRelayout,
	};

	enum : UINT {
		CmdInstallD3d11 = 100,
		CmdInstallDinput8,
		CmdInstallDxgi,
		CmdLaunchWith,
		CmdLaunchWithout,
		CmdBrowseFolder,
		CmdBrowseFile,
		CmdOpenFolder,
		CmdCopyPath,
		CmdRemove,
		CmdLanguageSystem,
		CmdLanguageEnglish,
		CmdLanguageKorean,
		CmdLanguageJapanese,
		CmdThemeSystem,
		CmdThemeLight,
		CmdThemeDark,
		CmdCheckUpdates,
		CmdHomepage,
		CmdOpenProgramDir,
		CmdOpenConfigDir,
		CmdCleanup,
	};

	constexpr UINT_PTR ProcessTimerId = 1;
	constexpr UINT ProcessTimerIntervalMs = 2000;
	constexpr auto MainWindowClassName = L"XivAlexander::Window::MainWindow";
	constexpr const wchar_t* InstallSlotNames[]{L"d3d11.dll", L"dinput8.dll", L"dxgi.dll"};

	struct InstallRow {
		Detector::FoundInstallation Found;
		Detector::InstallState State;
		bool Writable = false;
		bool Missing = false;  // a saved path where the game is no longer

		[[nodiscard]] std::filesystem::path GamePath() const { return Found.RootPath / L"game"; }
		[[nodiscard]] bool UserAdded() const { return Found.Sources & static_cast<uint32_t>(Detector::DiscoverySource::UserAdded); }
	};

	enum class ProcessState {
		NotLoaded,
		Loaded,
		LoadedByInstall,
		LoadedNoAccess,
		Unknown,
		Launcher,
	};

	struct ProcessRow {
		DWORD Pid{};
		std::wstring Name;
		std::filesystem::path Path;
		std::filesystem::path Root;  // of the installation it runs from, if found
		bool Is64Bit = true;
		bool IsGame = false;
		std::wstring Started;
		ProcessState State = ProcessState::Unknown;
		std::wstring Detail;  // the loaded version, or the DLL that loaded it
		bool NeedsAdmin = false;

		bool operator==(const ProcessRow&) const = default;
	};

	using Utils::EqualsI;

	bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
		return EqualsI(a.lexically_normal().native(), b.lexically_normal().native());
	}

	bool IsUnder(const std::filesystem::path& path, const std::filesystem::path& dir) {
		const auto p = path.lexically_normal().native();
		const auto d = dir.lexically_normal().native();
		return p.size() > d.size() && Utils::StartsWithI(p, d) && (p[d.size()] == L'\\' || p[d.size()] == L'/');
	}

	std::vector<int> VersionParts(const std::string& version) {
		std::vector<int> parts;
		for (const auto& part : xivres::util::split(version, std::string(".")))
			parts.push_back(std::atoi(part.c_str()));
		return parts;
	}

	std::wstring Trimmed(std::string s) {
		while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
			s.pop_back();
		return xivres::util::unicode::convert<std::wstring>(s);
	}

	std::wstring FormatProcessStartTime(HANDLE hProcess) {
		FILETIME created{}, exited{}, kernel{}, user{};
		if (!GetProcessTimes(hProcess, &created, &exited, &kernel, &user))
			return {};
		SYSTEMTIME utc{}, local{}, now{};
		if (!FileTimeToSystemTime(&created, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
			return {};
		GetLocalTime(&now);
		wchar_t time[64]{}, date[64]{};
		GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, static_cast<int>(std::size(time)));
		if (local.wYear == now.wYear && local.wMonth == now.wMonth && local.wDay == now.wDay)
			return time;
		GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, static_cast<int>(std::size(date)), nullptr);
		return std::format(L"{} {}", date, time);
	}

	bool HasMainWindow(DWORD pid) {
		struct Search {
			DWORD Pid;
			bool Found;
		} search{pid, false};
		EnumWindows([](HWND hWnd, LPARAM lParam) {
			auto& s = *reinterpret_cast<Search*>(lParam);
			DWORD windowPid{};
			GetWindowThreadProcessId(hWnd, &windowPid);
			if (windowPid != s.Pid)
				return TRUE;
			wchar_t className[64]{};
			GetClassNameW(hWnd, className, static_cast<int>(std::size(className)));
			if (wcscmp(className, MainWindowClassName) != 0)
				return TRUE;
			s.Found = true;
			return FALSE;
		}, reinterpret_cast<LPARAM>(&search));
		return search.Found;
	}

	// Which XivAlexander, if any, a running game has; without access, its control window tells.
	void ProbeLoadedState(ProcessRow& row) {
		const auto hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, row.Pid);
		if (hProcess) {
			try {
				const auto process = Utils::Win32::Process(hProcess, true);
				const auto exeDir = row.Path.parent_path();
				std::optional<std::filesystem::path> loaded;
				for (const auto hModule : process.EnumModules()) {
					const auto path = process.PathOf(hModule);
					const auto name = path.filename().wstring();
					if (EqualsI(name, XivAlexDll64NameW)) {
						loaded = path;
						continue;
					}
					if (!SamePath(path.parent_path(), exeDir) || std::ranges::none_of(InstallSlotNames, [&name](const auto slot) { return EqualsI(name, slot); }))
						continue;
					try {
						if (IsXivAlexanderDll(path)) {
							row.State = ProcessState::LoadedByInstall;
							row.Detail = name;
							return;
						}
					} catch (...) {
						// no version information; not ours
					}
				}
				if (loaded) {
					row.State = ProcessState::Loaded;
					try {
						row.Detail = xivres::util::unicode::convert<std::wstring>(Utils::Win32::FormatModuleVersionString(*loaded).first);
					} catch (...) {
						// version unknown
					}
				} else {
					row.State = ProcessState::NotLoaded;
				}
				return;
			} catch (...) {
				// the module list could not be read
			}
		}
		row.State = HasMainWindow(row.Pid) ? ProcessState::LoadedNoAccess : ProcessState::Unknown;
	}

	std::vector<ProcessRow> EnumerateProcesses(bool includeLaunchers, bool isAdmin) {
		std::vector<ProcessRow> rows;
		const auto hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE)
			return rows;
		const auto closeSnapshot = Utils::Win32::Handle(hSnapshot, true);

		PROCESSENTRY32W entry{.dwSize = sizeof entry};
		for (auto ok = Process32FirstW(hSnapshot, &entry); ok; ok = Process32NextW(hSnapshot, &entry)) {
			auto lower = std::wstring(entry.szExeFile);
			CharLowerW(lower.data());
			const auto isGame = lower == L"ffxiv_dx11.exe";
			if (!isGame && (!includeLaunchers || std::ranges::find(Detector::GameProgramNames(), lower) == Detector::GameProgramNames().end()))
				continue;

			ProcessRow row{.Pid = entry.th32ProcessID, .Name = entry.szExeFile, .IsGame = isGame};
			auto hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.Pid);
			if (!hProcess)
				hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, row.Pid);
			if (hProcess) {
				const auto closeProcess = Utils::Win32::Handle(hProcess, true);
				std::wstring path(PATHCCH_MAX_CCH, L'\0');
				auto length = static_cast<DWORD>(path.size());
				if (QueryFullProcessImageNameW(hProcess, 0, path.data(), &length)) {
					path.resize(length);
					row.Path = path;
					row.Root = Detector::FindRootStructurally(row.Path, 3).value_or(std::filesystem::path());
				}
				row.Started = FormatProcessStartTime(hProcess);
				USHORT processMachine{}, nativeMachine{};
				if (IsWow64Process2(hProcess, &processMachine, &nativeMachine))
					row.Is64Bit = processMachine == IMAGE_FILE_MACHINE_UNKNOWN;
			}

			// sdo's Launcher.exe is too common a name to count elsewhere.
			if (lower == L"launcher.exe" && row.Path.native().find(L"\\sdo\\sdologin\\") == std::wstring::npos)
				continue;

			if (isGame) {
				ProbeLoadedState(row);
				row.NeedsAdmin = !isAdmin && XivAlexander::LoaderApp::TestAdminRequirementForProcessManipulation({row.Pid});
			} else {
				row.State = ProcessState::Launcher;
			}
			rows.emplace_back(std::move(row));
		}
		std::ranges::sort(rows, [](const auto& l, const auto& r) {
			if (l.IsGame != r.IsGame)
				return l.IsGame;
			return l.Pid < r.Pid;
		});
		return rows;
	}

	std::filesystem::path PickPath(HWND hParent, const Ui::Strings& str, bool folder, const std::filesystem::path& startIn) {
		IFileOpenDialogPtr pDialog;
		Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
		DWORD flags{};
		Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&flags));
		if (folder) {
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST));
		} else {
			const COMDLG_FILTERSPEC fileTypes[]{
				{str(IDS_FILTERSPEC_FFXIVEXECUTABLEFILES), L"ffxivboot.exe;ffxivboot64.exe;ffxiv_dx11.exe;ffxivgame.ver;FFXIVBoot.exe;FFXIV_Boot.exe;FfxivLauncherTC.exe;ffxivlauncher*.exe"},
				{str(IDS_FILTERSPEC_EXECUTABLEFILES), L"*.exe"},
				{str(IDS_FILTERSPEC_ALLFILES), L"*"},
			};
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(static_cast<UINT>(std::size(fileTypes)), fileTypes));
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(1));
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST));
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(str(IDS_TITLE_SELECT_FFXIVEXECUTABLE)));
		}
		if (!startIn.empty()) {
			IShellItemPtr pFolder;
			if (SUCCEEDED(SHCreateItemFromParsingName(startIn.c_str(), nullptr, IID_PPV_ARGS(&pFolder))))
				pDialog->SetFolder(pFolder);
		}
		Utils::Win32::Error::ThrowIfFailed(pDialog->Show(hParent), true);

		IShellItemPtr pResult;
		Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
		PWSTR pszPath{};
		Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath));
		const auto freePath = xivres::util::on_dtor([pszPath] { CoTaskMemFree(pszPath); });
		return pszPath ? std::filesystem::path(pszPath) : std::filesystem::path();
	}

	class LoaderDialog {
	public:
		enum class Outcome {
			Closed,
			Restart,  // the language changed
		};

	private:
		const XivAlexander::LoaderApp::Arguments& m_args;
		const InstallUninstall::Paths m_paths;
		LoaderSettings m_settings;
		const Ui::Strings m_str;
		const bool m_isAdmin;
		const std::vector<int> m_loaderVersion;

		HWND m_hwnd{};
		Ui::DialogTheme m_theme;
		HICON m_hIconBig{}, m_hIconSmall{};
		std::unique_ptr<GridView> m_installGrid;
		std::unique_ptr<GridView> m_processGrid;
		SIZE m_minTrackSize{};
		int m_basicSessionLayout = -1;
		bool m_closed = false;
		Outcome m_outcome = Outcome::Closed;

		std::vector<InstallRow> m_installs;
		std::vector<ProcessRow> m_processes;
		std::vector<size_t> m_visibleProcesses;

		bool m_scanning = false;
		size_t m_discoveryGeneration = 0;
		std::mutex m_discoveredMtx;
		std::vector<InstallRow> m_discovered;
		std::jthread m_discovery;

		bool m_busy = false;
		std::thread m_operation;
		std::mutex m_operationMtx;
		std::function<void()> m_operationResult;

	public:
		LoaderDialog(const XivAlexander::LoaderApp::Arguments& args)
			: m_args(args)
			, m_paths(InstallUninstall::Paths::FromArguments(args))
			, m_settings(LoaderSettings::Load(m_paths.ConfigDir))
			, m_str(XivAlexander::LoaderApp::LangIdOf(m_settings.EffectiveLanguage(m_paths.ConfigDir)))
			, m_isAdmin(Utils::Win32::IsUserAnAdmin())
			, m_loaderVersion(VersionParts(Utils::Win32::FormatModuleVersionString(m_paths.SourceDir / XivAlexLoaderDll64NameW).first)) {
		}

		~LoaderDialog() {
			m_discovery = {};
			if (m_operation.joinable())
				m_operation.join();
			if (m_hIconBig)
				DestroyIcon(m_hIconBig);
			if (m_hIconSmall)
				DestroyIcon(m_hIconSmall);
		}

		Outcome Run() {
			if (!CreateDialogIndirectParamW(Module(), Ui::LoadDialogTemplate(IDD_LOADER_MAIN, m_str.LangId()), nullptr, &DialogProc, reinterpret_cast<LPARAM>(this)))
				throw Utils::Win32::Error("CreateDialogIndirectParamW");

			MSG msg{};
			while (!m_closed && GetMessageW(&msg, nullptr, 0, 0) > 0) {
				if (msg.message == WM_KEYDOWN && (msg.hwnd == m_hwnd || IsChild(m_hwnd, msg.hwnd)) && OnShortcutKey(static_cast<UINT>(msg.wParam)))
					continue;
				if (IsDialogMessageW(m_hwnd, &msg))
					continue;
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
			return m_outcome;
		}

	private:
		static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
			auto self = reinterpret_cast<LoaderDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			if (uMsg == WM_INITDIALOG) {
				self = reinterpret_cast<LoaderDialog*>(lParam);
				self->m_hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, lParam);
				self->OnInitDialog();
				return FALSE;
			}
			if (!self)
				return FALSE;
			try {
				return self->HandleMessage(uMsg, wParam, lParam);
			} catch (const std::exception& e) {
				self->SetStatus(self->m_str.Format(IDS_LOADER_STATUS_FAILED, xivres::util::unicode::convert<std::wstring>(e.what())));
				return TRUE;
			}
		}

		INT_PTR HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
			if (const auto result = m_theme.HandleMessage(m_hwnd, uMsg, wParam, lParam))
				return *result;

			switch (uMsg) {
				case WM_COMMAND:
					OnCommand(LOWORD(wParam), HIWORD(wParam));
					return TRUE;

				case WM_NOTIFY: {
					const auto& hdr = *reinterpret_cast<const NMHDR*>(lParam);
					if (hdr.code == BCN_DROPDOWN) {
						OnDropDown(static_cast<UINT>(hdr.idFrom));
						return TRUE;
					}
					return FALSE;
				}

				case WM_SIZE:
					Layout();
					return TRUE;

				case WM_GETMINMAXINFO:
					reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {m_minTrackSize.cx, m_minTrackSize.cy};
					return TRUE;

				case WM_NCHITTEST: {
					// As in the main window, the background moves the window.
					auto hit = DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
					if (hit == HTCLIENT)
						hit = HTCAPTION;
					SetWindowLongPtrW(m_hwnd, DWLP_MSGRESULT, hit);
					return TRUE;
				}

				case WM_DPICHANGED:
					// The dialog manager rescales the template's controls and font; the lists follow afterwards.
					PostMessageW(m_hwnd, WmRelayout, 0, 0);
					return FALSE;

				case WmRelayout:
					ApplyZoom();
					UpdateMinTrackSize();
					Layout();
					return TRUE;

				case WM_SETTINGCHANGE:
					if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
						ApplyTheme();
					return FALSE;

				case WM_TIMER:
					if (wParam == ProcessTimerId && !m_busy)
						RefreshProcesses();
					return TRUE;

				case WM_DROPFILES: {
					const auto hDrop = reinterpret_cast<HDROP>(wParam);
					std::vector<std::filesystem::path> paths;
					for (UINT i = 0, count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0); i < count; ++i) {
						std::wstring buf(static_cast<size_t>(DragQueryFileW(hDrop, i, nullptr, 0)) + 1, L'\0');
						buf.resize(DragQueryFileW(hDrop, i, buf.data(), static_cast<UINT>(buf.size())));
						paths.emplace_back(buf);
					}
					DragFinish(hDrop);
					for (const auto& path : paths)
						AddUserPath(path, is_directory(path));
					return TRUE;
				}

				case WmDiscovered:
					if (wParam == m_discoveryGeneration)
						OnDiscovered();
					return TRUE;

				case WmDiscoveryDone:
					if (wParam == m_discoveryGeneration)
						OnDiscoveryDone();
					return TRUE;

				case WmOperationDone:
					OnOperationDone();
					return TRUE;

				case WM_CLOSE:
					OnCommand(IDCANCEL, BN_CLICKED);
					return TRUE;

				case WM_DESTROY:
					KillTimer(m_hwnd, ProcessTimerId);
					m_discovery = {};
					m_installGrid.reset();
					m_processGrid.reset();
					m_closed = true;
					return FALSE;
			}
			return FALSE;
		}

		void OnInitDialog() {
			auto title = std::wstring(GetGenericMessageBoxTitle());
			if (m_isAdmin)
				title = m_str.Format(IDS_LOADER_TITLE_ADMIN, title);
			SetWindowTextW(m_hwnd, title.c_str());

			m_hIconBig = static_cast<HICON>(LoadImageW(Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
			m_hIconSmall = static_cast<HICON>(LoadImageW(Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
			SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_hIconBig));
			SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_hIconSmall));

			// Split buttons: Launch always, Install only in Advanced; More shows its menu wherever it is clicked.
			SetButtonSplit(IDC_LOADER_LAUNCH, true);
			SetButtonSplit(IDC_LOADER_INSTALL, m_settings.Advanced);
			SetButtonSplit(IDC_LOADER_BROWSE, true);
			SetButtonSplit(IDC_LOADER_MORE, true);
			BUTTON_SPLITINFO splitInfo{.mask = BCSIF_STYLE, .uSplitStyle = BCSS_NOSPLIT};
			Button_SetSplitInfo(GetDlgItem(m_hwnd, IDC_LOADER_MORE), &splitInfo);
			// The dialog clips children, so group boxes erase their own interior; they sit below their contents and clip them.
			for (const auto id : {IDC_LOADER_ONCE_GROUP, IDC_LOADER_AUTO_GROUP}) {
				const auto hGroup = GetDlgItem(m_hwnd, id);
				SetWindowLongPtrW(hGroup, GWL_STYLE, GetWindowLongPtrW(hGroup, GWL_STYLE) | WS_CLIPSIBLINGS);
				SetWindowSubclass(hGroup, GroupBoxSubclassProc, 1, reinterpret_cast<DWORD_PTR>(&m_theme));
			}
			ApplyMode();

			CheckDlgButton(m_hwnd, IDC_LOADER_ADVANCED, m_settings.Advanced ? BST_CHECKED : BST_UNCHECKED);
			ShowWindow(GetDlgItem(m_hwnd, IDC_LOADER_PROGRESS), SW_HIDE);
			DragAcceptFiles(m_hwnd, TRUE);

			CreateInstallGrid();
			CreateProcessGrid();
			ArrangeTabOrder();
			ApplyZoom();
			UpdateMinTrackSize();

			RECT rc{};
			GetWindowRect(m_hwnd, &rc);
			SetWindowPos(m_hwnd, nullptr, 0, 0, std::max<LONG>(rc.right - rc.left, m_minTrackSize.cx),
				m_settings.Advanced ? std::max<LONG>(rc.bottom - rc.top, m_minTrackSize.cy) : m_minTrackSize.cy,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			Ui::CenterOnMonitorOf(m_hwnd, nullptr);
			ApplyTheme();
			Layout();

			RefreshProcesses();
			StartDiscovery();
			SetTimer(m_hwnd, ProcessTimerId, ProcessTimerIntervalMs, nullptr);
			UpdateControls();

			ShowWindow(m_hwnd, SW_SHOW);
			SetFocus(m_installGrid->Handle());
		}

		bool OnShortcutKey(UINT vk) {
			const auto ctrl = GetKeyState(VK_CONTROL) < 0;
			if (vk == VK_F5 && !ctrl) {
				OnCommand(IDC_LOADER_REFRESH, BN_CLICKED);
				return true;
			}
			if (vk == 'O' && ctrl) {
				OnCommand(IDC_LOADER_BROWSE, BN_CLICKED);
				return true;
			}
			return false;
		}

		// Controls

		static LRESULT CALLBACK GroupBoxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
			const auto background = [hWnd](HDC hdc) {
				return reinterpret_cast<HBRUSH>(SendMessageW(GetParent(hWnd), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(hdc), reinterpret_cast<LPARAM>(hWnd)));
			};
			RECT rc{};
			GetClientRect(hWnd, &rc);
			if (uMsg == WM_ERASEBKGND) {
				FillRect(reinterpret_cast<HDC>(wParam), &rc, background(reinterpret_cast<HDC>(wParam)));
				return TRUE;
			}
			// Dark mode turns the theme off for coloured captions, which leaves the classic etched frame; draw a flat one.
			if (uMsg != WM_PAINT || !reinterpret_cast<const Ui::DialogTheme*>(refData)->Dark())
				return DefSubclassProc(hWnd, uMsg, wParam, lParam);

			PAINTSTRUCT ps;
			const auto hdc = BeginPaint(hWnd, &ps);
			const auto brush = background(hdc);
			const auto hPrevFont = SelectObject(hdc, reinterpret_cast<HFONT>(SendMessageW(hWnd, WM_GETFONT, 0, 0)));
			const auto text = Utils::Win32::GetWindowString(hWnd);
			SIZE extent{};
			GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &extent);
			const auto dpi = static_cast<int>(GetDpiForWindow(hWnd));
			const auto& colors = Window::GetThemeColors(true);

			const auto hFrameBrush = CreateSolidBrush(colors.ForegroundWeak);
			RECT rcFrame{rc.left, rc.top + extent.cy / 2, rc.right, rc.bottom};
			FrameRect(hdc, &rcFrame, hFrameBrush);
			DeleteObject(hFrameBrush);

			const auto indent = MulDiv(8, dpi, 96);
			const auto pad = MulDiv(2, dpi, 96);
			RECT rcText{rc.left + indent - pad, rc.top, rc.left + indent + extent.cx + pad, rc.top + extent.cy};
			FillRect(hdc, &rcText, brush);
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, colors.GetForeground());
			DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(hdc, hPrevFont);
			EndPaint(hWnd, &ps);
			return 0;
		}

		void SetButtonSplit(int id, bool split) {
			const auto hButton = GetDlgItem(m_hwnd, id);
			const auto fullStyle = static_cast<DWORD>(GetWindowLongPtrW(hButton, GWL_STYLE));
			const auto type = fullStyle & BS_TYPEMASK;
			const auto isDefault = type == BS_DEFPUSHBUTTON || type == BS_DEFSPLITBUTTON;
			const auto wanted = static_cast<DWORD>(split ? (isDefault ? BS_DEFSPLITBUTTON : BS_SPLITBUTTON) : (isDefault ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON));
			if (type == wanted)
				return;
			// BM_SETSTYLE alone leaves the split part drawn when going back to a push button.
			SetWindowLongPtrW(hButton, GWL_STYLE, (fullStyle & ~BS_TYPEMASK) | wanted);
			SendMessageW(hButton, BM_SETSTYLE, wanted, FALSE);
			SetWindowPos(hButton, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			InvalidateRect(hButton, nullptr, TRUE);
		}

		void ApplyTheme() {
			const auto dark = Window::IsDarkModeEnabled(m_settings.EffectiveThemeMode(m_paths.ConfigDir));
			m_theme.Apply(m_hwnd, dark);
			if (m_installGrid)
				m_installGrid->ApplyTheme(dark);
			if (m_processGrid)
				m_processGrid->ApplyTheme(dark);
		}

		void ApplyZoom() {
			const auto hFont = reinterpret_cast<HFONT>(SendMessageW(m_hwnd, WM_GETFONT, 0, 0));
			const auto zoom = GetDpiForWindow(m_hwnd) / 96.0;
			if (m_installGrid)
				m_installGrid->SetZoom(zoom, hFont);
			if (m_processGrid)
				m_processGrid->SetZoom(zoom, hFont);
		}

		void ApplyMode() {
			const auto advanced = m_settings.Advanced;
			m_basicSessionLayout = -1;
			for (const auto id : {IDC_LOADER_INTRO, IDC_LOADER_METHOD_LABEL, IDC_LOADER_ONCE_GROUP, IDC_LOADER_ONCE_NOTE,
				IDC_LOADER_AUTO_GROUP, IDC_LOADER_AUTO_NOTE, IDC_LOADER_APPLY_ONCE, IDC_LOADER_GUIDANCE})
				ShowWindow(GetDlgItem(m_hwnd, id), advanced ? SW_HIDE : SW_SHOWNOACTIVATE);
			for (const auto id : {IDC_LOADER_REMOVE, IDC_LOADER_LAUNCH, IDC_LOADER_SHOWCONTROLWINDOW, IDC_LOADER_LOAD, IDC_LOADER_INSTALL_DETAILS})
				ShowWindow(GetDlgItem(m_hwnd, id), advanced ? SW_SHOWNOACTIVATE : SW_HIDE);
			if (advanced)
				for (const auto id : {IDC_LOADER_PROCESSES_LABEL, IDC_LOADER_PROCESS_DETAILS, IDC_LOADER_UNLOAD})
					ShowWindow(GetDlgItem(m_hwnd, id), SW_SHOWNOACTIVATE);
			SetDlgItemTextW(m_hwnd, IDC_LOADER_INSTALLS_LABEL, m_str(advanced ? IDS_LOADER_LABEL_INSTALLATIONS : IDS_LOADER_STEP_GAME));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_PROCESSES_LABEL, m_str(advanced ? IDS_LOADER_LABEL_PROCESSES : IDS_LOADER_LABEL_SESSIONS));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_BROWSE, m_str(advanced ? IDS_LOADER_BUTTON_BROWSE : IDS_LOADER_BUTTON_FIND_GAME));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_UNINSTALL, m_str(advanced ? IDS_LOADER_BUTTON_UNINSTALL : IDS_LOADER_STOP_AUTO));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_UNLOAD, m_str(advanced ? IDS_LOADER_BUTTON_UNLOAD : IDS_LOADER_STOP_ONCE));
			const auto status = GetDlgItem(m_hwnd, IDC_LOADER_STATUS);
			const auto style = GetWindowLongPtrW(status, GWL_STYLE);
			SetWindowLongPtrW(status, GWL_STYLE, (style & ~SS_TYPEMASK) | (advanced ? SS_LEFTNOWORDWRAP : SS_LEFT));
		}

		void ArrangeTabOrder() {
			const auto basic = std::initializer_list<int>{IDC_LOADER_INTRO, IDC_LOADER_INSTALLS_LABEL, IDC_LOADER_INSTALLS_LIST,
				IDC_LOADER_BROWSE, IDC_LOADER_REFRESH, IDC_LOADER_METHOD_LABEL, IDC_LOADER_ONCE_GROUP, IDC_LOADER_ONCE_NOTE,
				IDC_LOADER_APPLY_ONCE, IDC_LOADER_AUTO_GROUP, IDC_LOADER_AUTO_NOTE, IDC_LOADER_INSTALL, IDC_LOADER_GUIDANCE,
				IDC_LOADER_UNINSTALL, IDC_LOADER_PROCESSES_LABEL, IDC_LOADER_PROCESSES_LIST, IDC_LOADER_PROCESS_DETAILS,
				IDC_LOADER_UNLOAD, IDC_LOADER_STATUS, IDC_LOADER_PROGRESS, IDC_LOADER_ADVANCED, IDC_LOADER_MORE, IDCANCEL};
			const auto advanced = std::initializer_list<int>{IDC_LOADER_INSTALLS_LABEL, IDC_LOADER_INSTALLS_LIST, IDC_LOADER_INSTALL_DETAILS,
				IDC_LOADER_SHOWCONTROLWINDOW, IDC_LOADER_BROWSE, IDC_LOADER_REMOVE, IDC_LOADER_REFRESH, IDC_LOADER_LAUNCH,
				IDC_LOADER_UNINSTALL, IDC_LOADER_INSTALL, IDC_LOADER_PROCESSES_LABEL, IDC_LOADER_PROCESSES_LIST,
				IDC_LOADER_PROCESS_DETAILS, IDC_LOADER_UNLOAD, IDC_LOADER_LOAD, IDC_LOADER_ADVANCED, IDC_LOADER_STATUS,
				IDC_LOADER_PROGRESS, IDC_LOADER_MORE, IDCANCEL};
			auto previous = HWND_TOP;
			for (const auto id : m_settings.Advanced ? advanced : basic) {
				const auto hwnd = GetDlgItem(m_hwnd, id);
				SetWindowPos(hwnd, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				previous = hwnd;
			}
			for (const auto id : {IDC_LOADER_ONCE_GROUP, IDC_LOADER_AUTO_GROUP})
				SetWindowPos(GetDlgItem(m_hwnd, id), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		void CreateInstallGrid() {
			const auto selected = SelectedInstall() ? SelectedInstall()->Found.RootPath : std::filesystem::path();
			m_installGrid.reset();
			std::vector<GridView::Column> columns{
				{.Title = m_str(IDS_LOADER_COL_LOCATION), .Kind = GridView::CellKind::ReadOnly, .Width = 160, .Fill = true},
				{.Title = m_str(IDS_LOADER_COL_REGION), .Kind = GridView::CellKind::ReadOnly, .Width = 110},
				{.Title = m_str(IDS_LOADER_COL_VERSION), .Kind = GridView::CellKind::ReadOnly, .Width = 130},
				{.Title = m_str(IDS_LOADER_COL_STATE), .Kind = GridView::CellKind::ReadOnly, .Width = 220},
			};
			if (m_settings.Advanced)
				columns.push_back({.Title = m_str(IDS_LOADER_COL_FOUNDVIA), .Kind = GridView::CellKind::ReadOnly, .Width = 150});
			else
				columns = {
					{.Title = m_str(IDS_LOADER_COL_REGION), .Kind = GridView::CellKind::ReadOnly, .Width = 130},
					{.Title = m_str(IDS_LOADER_AUTO_GROUP), .Kind = GridView::CellKind::ReadOnly, .Width = 170},
					{.Title = m_str(IDS_LOADER_COL_LOCATION), .Kind = GridView::CellKind::ReadOnly, .Width = 220, .Fill = true},
				};
			m_installGrid = std::make_unique<GridView>(m_hwnd, IDC_LOADER_INSTALLS_LIST, std::move(columns), GridView::Source{
				.GetRowCount = [this] { return m_installs.size(); },
				.GetText = [this](size_t row, size_t column) { return InstallCellText(m_installs[row], column); },
				.Activate = [this](size_t) {
					if (m_settings.Advanced)
						Launch(true);
					else
						SetFocus(GetDlgItem(m_hwnd, IDC_LOADER_APPLY_ONCE));
				},
				.ShowContextMenu = [this](size_t row, POINT pt) { ShowInstallContextMenu(row, pt); },
				.SelectionChanged = [this](std::optional<size_t>) { RefreshProcessView(); UpdateControls(); },
				.KeyDown = [this](UINT vk) {
					if (vk != VK_DELETE)
						return false;
					if (const auto row = SelectedInstall(); row && row->UserAdded())
						RemoveSelectedFromList();
					return true;
				},
				.GetEmptyText = [this] {
					return std::wstring(m_str(m_scanning ? IDS_LOADER_EMPTY_SCANNING
						: m_settings.Advanced ? IDS_LOADER_EMPTY_NOINSTALL : IDS_LOADER_EMPTY_GAME));
				},
			});
			SetWindowPos(m_installGrid->Handle(), GetDlgItem(m_hwnd, IDC_LOADER_INSTALLS_LABEL), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			SelectInstallByRoot(selected);
		}

		void CreateProcessGrid() {
			const auto selectedPid = SelectedProcess() ? SelectedProcess()->Pid : 0;
			m_processGrid.reset();
			std::vector<GridView::Column> columns{{.Title = m_str(IDS_LOADER_COL_PID), .Kind = GridView::CellKind::ReadOnly, .Width = 70}};
			if (m_settings.Advanced) {
				columns.push_back({.Title = m_str(IDS_LOADER_COL_PROGRAM), .Kind = GridView::CellKind::ReadOnly, .Width = 130});
				columns.push_back({.Title = m_str(IDS_LOADER_COL_ARCH), .Kind = GridView::CellKind::ReadOnly, .Width = 100});
			}
			else
				columns.clear();
			columns.push_back({.Title = m_str(IDS_LOADER_COL_INSTALLATION), .Kind = GridView::CellKind::ReadOnly, .Width = 160, .Fill = true});
			columns.push_back({.Title = m_str(IDS_LOADER_COL_STARTED), .Kind = GridView::CellKind::ReadOnly, .Width = 120});
			columns.push_back({.Title = m_str(IDS_LOADER_COL_STATE), .Kind = GridView::CellKind::ReadOnly, .Width = 220});
			m_processGrid = std::make_unique<GridView>(m_hwnd, IDC_LOADER_PROCESSES_LIST, std::move(columns), GridView::Source{
				.GetRowCount = [this] { return m_visibleProcesses.size(); },
				.GetText = [this](size_t row, size_t column) { return ProcessCellText(m_processes[m_visibleProcesses[row]], column); },
				.Activate = [this](size_t) {
					if (m_settings.Advanced && IsWindowEnabled(GetDlgItem(m_hwnd, IDC_LOADER_LOAD)))
						LoadOrUnload(true);
					else if (!m_settings.Advanced)
						SetFocus(GetDlgItem(m_hwnd, IDC_LOADER_APPLY_ONCE));
				},
				.SelectionChanged = [this](std::optional<size_t>) { UpdateControls(); },
				.GetEmptyText = [this] { return std::wstring(m_str(m_settings.Advanced ? IDS_LOADER_EMPTY_NOPROCESS : IDS_LOADER_EMPTY_NOSESSION)); },
			});
			SetWindowPos(m_processGrid->Handle(), GetDlgItem(m_hwnd, IDC_LOADER_PROCESSES_LABEL), 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			RefreshProcessView(selectedPid);
		}

		// Layout, in dialog units converted for the current font.

		[[nodiscard]] int Dlu(int x, bool vertical = false) const {
			RECT rc{0, 0, vertical ? 0 : x, vertical ? x : 0};
			MapDialogRect(m_hwnd, &rc);
			return vertical ? rc.bottom : rc.right;
		}

		[[nodiscard]] int ButtonWidth(int id) const {
			const auto hButton = GetDlgItem(m_hwnd, id);
			const auto style = static_cast<DWORD>(GetWindowLongPtrW(hButton, GWL_STYLE)) & BS_TYPEMASK;
			auto width = Ui::MeasureTextWidth(hButton);
			if (style == BS_AUTOCHECKBOX || style == BS_CHECKBOX)
				return width + Dlu(16);
			width += Dlu(14);
			if (style == BS_SPLITBUTTON || style == BS_DEFSPLITBUTTON)
				width += Dlu(12);
			if (id == IDC_LOADER_INSTALL || id == IDC_LOADER_UNINSTALL || id == IDC_LOADER_LOAD || id == IDC_LOADER_UNLOAD)
				width += Dlu(10);  // room for the shield
			return std::max(width, Dlu(50));
		}

		struct Metrics {
			int Margin, Gap, ButtonHeight, LabelHeight, CheckHeight, InstallDetailsHeight, ProcessDetailsHeight, SectionGap;
			int TextHeight;
			int Fixed;
			int MinInstallList, MinProcessList;
		};

		[[nodiscard]] int TextLineHeight() const {
			const auto hdc = GetDC(m_hwnd);
			const auto hPrevFont = SelectObject(hdc, reinterpret_cast<HFONT>(SendMessageW(m_hwnd, WM_GETFONT, 0, 0)));
			TEXTMETRICW tm{};
			GetTextMetricsW(hdc, &tm);
			SelectObject(hdc, hPrevFont);
			ReleaseDC(m_hwnd, hdc);
			return static_cast<int>(tm.tmHeight);
		}

		[[nodiscard]] Metrics GetMetrics() const {
			Metrics m{
				.Margin = Dlu(7),
				.Gap = Dlu(4),
				.ButtonHeight = Dlu(14, true),
				.LabelHeight = Dlu(10, true),
				.CheckHeight = Dlu(12, true),
				.InstallDetailsHeight = Dlu(26, true),
				.ProcessDetailsHeight = Dlu(18, true),
				.SectionGap = Dlu(8, true),
				.TextHeight = TextLineHeight(),
				.MinInstallList = Dlu(56, true),
				.MinProcessList = Dlu(36, true),
			};
			const auto tight = Dlu(3, true);
			m.Fixed = m.Margin + m.LabelHeight + tight + tight + m.InstallDetailsHeight + tight + m.ButtonHeight + m.SectionGap
				+ m.LabelHeight + tight + tight + m.ProcessDetailsHeight + m.SectionGap + m.ButtonHeight + m.Margin;
			return m;
		}

		void UpdateMinTrackSize() {
			const auto m = GetMetrics();
			auto row1 = 0;
			for (const auto id : {IDC_LOADER_BROWSE, IDC_LOADER_REMOVE, IDC_LOADER_REFRESH, IDC_LOADER_LAUNCH, IDC_LOADER_UNINSTALL, IDC_LOADER_INSTALL})
				row1 += ButtonWidth(id) + m.Gap;
			auto row3 = ButtonWidth(IDC_LOADER_ADVANCED) + ButtonWidth(IDC_LOADER_MORE) + ButtonWidth(IDCANCEL) + Dlu(120);
			RECT rc{0, 0, std::max(row1 + m.Gap * 4, row3) + m.Margin * 2, m.Fixed + m.MinInstallList + m.MinProcessList};
			if (!m_settings.Advanced)
				rc = {0, 0, Dlu(480), BasicFixedHeight() + m.MinInstallList + (m_visibleProcesses.size() > 1 ? m.MinProcessList : 0)};
			AdjustWindowRectExForDpi(&rc, static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE)), FALSE, static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE)), GetDpiForWindow(m_hwnd));
			m_minTrackSize = {rc.right - rc.left, rc.bottom - rc.top};
		}

		void Layout() {
			if (!m_installGrid || !m_processGrid)
				return;
			if (!m_settings.Advanced) {
				LayoutBasic();
				return;
			}
			RECT rcClient{};
			GetClientRect(m_hwnd, &rcClient);
			const auto width = rcClient.right;
			const auto m = GetMetrics();
			const auto tight = Dlu(3, true);
			const auto lists = std::max(0, static_cast<int>(rcClient.bottom) - m.Fixed);
			const auto installListHeight = std::max(m.MinInstallList, lists * 3 / 5);
			const auto processListHeight = std::max(m.MinProcessList, lists - installListHeight);

			const auto hdwp = BeginDeferWindowPos(24);
			auto dwp = hdwp;
			const auto place = [&](HWND hWnd, int x, int y, int w, int h) {
				if (dwp)
					dwp = DeferWindowPos(dwp, hWnd, nullptr, x, y, std::max(0, w), std::max(0, h), SWP_NOZORDER | SWP_NOACTIVATE);
			};
			const auto item = [this](int id) { return GetDlgItem(m_hwnd, id); };
			const auto inner = width - m.Margin * 2;

			auto y = m.Margin;
			place(item(IDC_LOADER_INSTALLS_LABEL), m.Margin, y, inner, m.LabelHeight);
			y += m.LabelHeight + tight;
			place(m_installGrid->Handle(), m.Margin, y, inner, installListHeight);
			y += installListHeight + tight;

			// Text lines sit where the text of the checkboxes and buttons beside them is vertically centred.
			const auto checkWidth = ButtonWidth(IDC_LOADER_SHOWCONTROLWINDOW);
			const auto installDetailsShift = (m.CheckHeight - m.TextHeight) / 2;
			place(item(IDC_LOADER_INSTALL_DETAILS), m.Margin, y + installDetailsShift, inner - checkWidth - m.Gap, m.InstallDetailsHeight - installDetailsShift);
			place(item(IDC_LOADER_SHOWCONTROLWINDOW), width - m.Margin - checkWidth, y, checkWidth, m.CheckHeight);
			y += m.InstallDetailsHeight + tight;

			auto x = m.Margin;
			for (const auto id : {IDC_LOADER_BROWSE, IDC_LOADER_REMOVE, IDC_LOADER_REFRESH}) {
				const auto w = ButtonWidth(id);
				place(item(id), x, y, w, m.ButtonHeight);
				x += w + m.Gap;
			}
			x = width - m.Margin;
			for (const auto id : {IDC_LOADER_INSTALL, IDC_LOADER_UNINSTALL, IDC_LOADER_LAUNCH}) {
				const auto w = ButtonWidth(id);
				x -= w;
				place(item(id), x, y, w, m.ButtonHeight);
				x -= m.Gap;
			}
			y += m.ButtonHeight + m.SectionGap;

			place(item(IDC_LOADER_PROCESSES_LABEL), m.Margin, y, inner, m.LabelHeight);
			y += m.LabelHeight + tight;
			place(m_processGrid->Handle(), m.Margin, y, inner, processListHeight);
			y += processListHeight + tight;

			x = width - m.Margin;
			for (const auto id : {IDC_LOADER_LOAD, IDC_LOADER_UNLOAD}) {
				const auto w = ButtonWidth(id);
				x -= w;
				place(item(id), x, y, w, m.ButtonHeight);
				x -= m.Gap;
			}
			place(item(IDC_LOADER_PROCESS_DETAILS), m.Margin, y + (m.ButtonHeight - m.TextHeight) / 2, x - m.Margin - m.Gap, m.ProcessDetailsHeight);
			y += m.ProcessDetailsHeight + m.SectionGap;

			const auto bottom = static_cast<int>(rcClient.bottom) - m.Margin - m.ButtonHeight;
			y = std::max(y, bottom);
			const auto advancedWidth = ButtonWidth(IDC_LOADER_ADVANCED);
			place(item(IDC_LOADER_ADVANCED), m.Margin, y, advancedWidth, m.ButtonHeight);
			x = width - m.Margin;
			for (const auto id : {IDCANCEL, IDC_LOADER_MORE}) {
				const auto w = ButtonWidth(id);
				x -= w;
				place(item(id), x, y, w, m.ButtonHeight);
				x -= m.Gap;
			}
			const auto progressWidth = Dlu(50);
			x -= progressWidth;
			place(item(IDC_LOADER_PROGRESS), x, y + (m.ButtonHeight - m.LabelHeight) / 2, progressWidth, m.LabelHeight);
			x -= m.Gap;
			const auto statusLeft = m.Margin + advancedWidth + m.Gap * 2;
			place(item(IDC_LOADER_STATUS), statusLeft, y + (m.ButtonHeight - m.TextHeight) / 2, x - statusLeft, m.TextHeight);

			if (dwp)
				EndDeferWindowPos(dwp);
			InvalidateRect(m_hwnd, nullptr, TRUE);
		}

		[[nodiscard]] int BasicFixedHeight() const {
			return Dlu(234 + (m_visibleProcesses.empty() ? 0 : 24) + (m_visibleProcesses.size() > 1 ? 16 : 0), true);
		}

		void UpdateBasicLayout() {
			const auto sessionLayout = m_visibleProcesses.empty() ? 0 : m_visibleProcesses.size() == 1 ? 1 : 2;
			if (sessionLayout == m_basicSessionLayout)
				return;
			m_basicSessionLayout = sessionLayout;
			ShowWindow(m_processGrid->Handle(), sessionLayout == 2 ? SW_SHOWNOACTIVATE : SW_HIDE);
			ShowWindow(GetDlgItem(m_hwnd, IDC_LOADER_PROCESSES_LABEL), sessionLayout == 2 ? SW_SHOWNOACTIVATE : SW_HIDE);
			ShowWindow(GetDlgItem(m_hwnd, IDC_LOADER_PROCESS_DETAILS), sessionLayout ? SW_SHOWNOACTIVATE : SW_HIDE);
			ShowWindow(GetDlgItem(m_hwnd, IDC_LOADER_UNLOAD), sessionLayout ? SW_SHOWNOACTIVATE : SW_HIDE);
			UpdateMinTrackSize();
			RECT rc{};
			GetWindowRect(m_hwnd, &rc);
			if (rc.right - rc.left < m_minTrackSize.cx || rc.bottom - rc.top < m_minTrackSize.cy)
				SetWindowPos(m_hwnd, nullptr, 0, 0, std::max<LONG>(rc.right - rc.left, m_minTrackSize.cx), std::max<LONG>(rc.bottom - rc.top, m_minTrackSize.cy), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
		}

		void LayoutBasic() {
			RECT rc{};
			GetClientRect(m_hwnd, &rc);
			const auto margin = Dlu(10);
			const auto gap = Dlu(6);
			const auto width = static_cast<int>(rc.right);
			const auto inner = width - margin * 2;
			const auto m = GetMetrics();
			const auto lists = std::max(0, static_cast<int>(rc.bottom) - BasicFixedHeight());
			const auto showProcessList = m_visibleProcesses.size() > 1;
			const auto installHeight = std::max(m.MinInstallList, showProcessList ? lists * 3 / 5 : lists);
			const auto processHeight = showProcessList ? std::max(m.MinProcessList, lists - installHeight) : 0;
			auto dwp = BeginDeferWindowPos(24);
			const auto place = [&](HWND hwnd, int x, int y, int w, int h) {
				if (dwp)
					dwp = DeferWindowPos(dwp, hwnd, nullptr, x, y, std::max(0, w), std::max(0, h), SWP_NOZORDER | SWP_NOACTIVATE);
			};
			const auto item = [this](int id) { return GetDlgItem(m_hwnd, id); };
			const auto line = [&](int id, int y, int height) { place(item(id), margin, y, inner, Dlu(height, true)); };
			auto y = Dlu(8, true);
			line(IDC_LOADER_INTRO, y, 18);
			y += Dlu(24, true);
			const auto browseWidth = ButtonWidth(IDC_LOADER_BROWSE);
			const auto refreshWidth = ButtonWidth(IDC_LOADER_REFRESH);
			place(item(IDC_LOADER_INSTALLS_LABEL), margin, y + Dlu(3, true), inner - browseWidth - refreshWidth - gap * 2, Dlu(10, true));
			place(item(IDC_LOADER_BROWSE), width - margin - refreshWidth - gap - browseWidth, y, browseWidth, Dlu(16, true));
			place(item(IDC_LOADER_REFRESH), width - margin - refreshWidth, y, refreshWidth, Dlu(16, true));
			y += Dlu(19, true);
			place(m_installGrid->Handle(), margin, y, inner, installHeight);
			y += installHeight + Dlu(6, true);
			line(IDC_LOADER_METHOD_LABEL, y, 10);
			y += Dlu(13, true);
			const auto cardWidth = (inner - gap) / 2;
			const auto cardHeight = Dlu(74, true);
			for (const auto automatic : {false, true}) {
				const auto left = automatic ? margin + cardWidth + gap : margin;
				place(item(automatic ? IDC_LOADER_AUTO_GROUP : IDC_LOADER_ONCE_GROUP), left, y, cardWidth, cardHeight);
				place(item(automatic ? IDC_LOADER_AUTO_NOTE : IDC_LOADER_ONCE_NOTE), left + gap, y + Dlu(14, true), cardWidth - gap * 2, Dlu(32, true));
				const auto action = automatic ? IDC_LOADER_INSTALL : IDC_LOADER_APPLY_ONCE;
				place(item(action), left + gap, y + Dlu(50, true), cardWidth - gap * 2, Dlu(18, true));
			}
			y += cardHeight + Dlu(6, true);
			// The undo action sits next to the instruction, away from the initial installation action.
			const auto uninstallWidth = ButtonWidth(IDC_LOADER_UNINSTALL);
			place(item(IDC_LOADER_GUIDANCE), margin, y, inner - uninstallWidth - gap, Dlu(30, true));
			place(item(IDC_LOADER_UNINSTALL), width - margin - uninstallWidth, y, uninstallWidth, Dlu(18, true));
			y += Dlu(36, true);
			if (showProcessList) {
				line(IDC_LOADER_PROCESSES_LABEL, y, 10);
				y += Dlu(13, true);
				place(m_processGrid->Handle(), margin, y, inner, processHeight);
				y += processHeight + Dlu(3, true);
			}
			if (!m_visibleProcesses.empty()) {
				const auto unloadWidth = ButtonWidth(IDC_LOADER_UNLOAD);
				place(item(IDC_LOADER_PROCESS_DETAILS), margin, y, inner - unloadWidth - gap, Dlu(18, true));
				place(item(IDC_LOADER_UNLOAD), width - margin - unloadWidth, y, unloadWidth, Dlu(18, true));
				y += Dlu(24, true);
			}
			place(item(IDC_LOADER_STATUS), margin, y, inner - Dlu(56), Dlu(18, true));
			place(item(IDC_LOADER_PROGRESS), width - margin - Dlu(50), y, Dlu(50), Dlu(10, true));
			const auto bottom = std::max(y + Dlu(24, true), static_cast<int>(rc.bottom) - Dlu(24, true));
			place(item(IDC_LOADER_ADVANCED), margin, bottom, ButtonWidth(IDC_LOADER_ADVANCED), Dlu(16, true));
			auto x = width - margin;
			for (const auto id : {IDCANCEL, IDC_LOADER_MORE}) {
				const auto w = ButtonWidth(id);
				x -= w;
				place(item(id), x, bottom, w, Dlu(16, true));
				x -= gap;
			}
			if (dwp)
				EndDeferWindowPos(dwp);
			RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		}

		// Texts

		[[nodiscard]] std::wstring RegionName(xivres::game_release_publisher region) const {
			switch (region) {
				case xivres::game_release_publisher::SquareEnix: return m_str(IDS_CLIENT_INTERNATIONAL);
				case xivres::game_release_publisher::ActozSoft: return m_str(IDS_CLIENT_ACTOZSOFT);
				case xivres::game_release_publisher::ShengquGames: return m_str(IDS_CLIENT_SHENGQUGAMES);
				case xivres::game_release_publisher::UserjoyGames: return m_str(IDS_CLIENT_USERJOYGAMES);
				default: return m_str(IDS_LOADER_REGION_UNKNOWN);
			}
		}

		[[nodiscard]] std::wstring SourceName(Detector::DiscoverySource source) const {
			using enum Detector::DiscoverySource;
			switch (source) {
				case RegistrySquareEnix: return m_str(IDS_LOADER_FOUNDVIA_SQUAREENIX);
				case Steam: return m_str(IDS_LOADER_FOUNDVIA_STEAM);
				case RegistryActoz: return m_str(IDS_LOADER_FOUNDVIA_ACTOZ);
				case RegistryShengqu: return m_str(IDS_LOADER_FOUNDVIA_SHENGQU);
				case RegistryUserjoy: return m_str(IDS_LOADER_FOUNDVIA_USERJOY);
				case MsiComponent: return m_str(IDS_LOADER_FOUNDVIA_MSI);
				case KnownFolder: return m_str(IDS_LOADER_FOUNDVIA_KNOWNFOLDER);
				case XivLauncher: return m_str(IDS_LOADER_FOUNDVIA_XIVLAUNCHER);
				case ShortcutDesktop: return m_str(IDS_LOADER_FOUNDVIA_DESKTOP);
				case ShortcutStartMenu: return m_str(IDS_LOADER_FOUNDVIA_STARTMENU);
				case RunningProcess: return m_str(IDS_LOADER_FOUNDVIA_PROCESS);
				case XivAlexanderConfig: return m_str(IDS_LOADER_FOUNDVIA_CONFIG);
				case UserAdded: return m_str(IDS_LOADER_FOUNDVIA_USER);
				case CommandLine: return m_str(IDS_LOADER_FOUNDVIA_COMMANDLINE);
			}
			return {};
		}

		[[nodiscard]] std::wstring SourcesText(uint32_t sources) const {
			std::wstring text;
			for (uint32_t bit = 1; bit && bit <= sources; bit <<= 1) {
				if (!(sources & bit))
					continue;
				if (!text.empty())
					text += L", ";
				text += SourceName(static_cast<Detector::DiscoverySource>(bit));
			}
			return text;
		}

		[[nodiscard]] std::wstring StateText(const InstallRow& row) const {
			if (row.Missing)
				return m_str(IDS_LOADER_STATE_MISSING);
			if (const auto dll = row.State.Installed()) {
				const auto version = VersionParts(dll->Version);
				if (!m_settings.Advanced)
					return m_str(dll->IsOldStyleXivAlexander || version < m_loaderVersion ? IDS_LOADER_AUTO_OLDER : IDS_LOADER_AUTO_READY);
				const auto id = dll->IsOldStyleXivAlexander || version < m_loaderVersion ? IDS_LOADER_STATE_OLDER : m_loaderVersion < version ? IDS_LOADER_STATE_NEWER : IDS_LOADER_STATE_INSTALLED;
				return m_str.Format(id, dll->Name, xivres::util::unicode::convert<std::wstring>(dll->Version));
			}
			for (const auto& dll : row.State.Dlls) {
				if (dll.Kind == Detector::ManagedDllKind::XivAlexander)
					return m_settings.Advanced ? m_str.Format(IDS_LOADER_STATE_LEFTOVER, dll.Name) : m_str(IDS_LOADER_AUTO_INCOMPLETE);
			}
			return m_str(m_settings.Advanced ? IDS_LOADER_STATE_NOTINSTALLED : IDS_LOADER_AUTO_OFF);
		}

		[[nodiscard]] std::wstring InstallCellText(const InstallRow& row, size_t column) const {
			if (!m_settings.Advanced) {
				switch (column) {
					case 0: return row.Missing ? m_str(IDS_LOADER_STATE_MISSING) : RegionName(row.Found.Info.Region);
					case 1: return StateText(row);
					case 2: return row.Found.RootPath.wstring();
				}
				return {};
			}
			switch (column) {
				case 0: return row.Found.RootPath.wstring();
				case 1: return row.Missing ? std::wstring() : RegionName(row.Found.Info.Region);
				case 2: return Trimmed(row.Found.Info.GameVersion);
				case 3: return StateText(row);
				case 4: return SourcesText(row.Found.Sources);
			}
			return {};
		}

		[[nodiscard]] std::wstring ProcessStateText(const ProcessRow& row) const {
			if (!m_settings.Advanced) {
				switch (row.State) {
					case ProcessState::NotLoaded: return m_str(IDS_LOADER_SESSION_OFF);
					case ProcessState::Loaded:
					case ProcessState::LoadedNoAccess:
					case ProcessState::LoadedByInstall: return m_str(IDS_LOADER_SESSION_ON);
					default: return m_str(IDS_LOADER_SESSION_UNKNOWN);
				}
			}
			switch (row.State) {
				case ProcessState::NotLoaded: return m_str(IDS_LOADER_PSTATE_NOTLOADED);
				case ProcessState::Loaded: return m_str.Format(IDS_LOADER_PSTATE_LOADED, row.Detail);
				case ProcessState::LoadedByInstall: return m_str.Format(IDS_LOADER_PSTATE_LOADEDBYINSTALL, row.Detail);
				case ProcessState::LoadedNoAccess: return m_str(IDS_LOADER_PSTATE_LOADEDNOACCESS);
				case ProcessState::Unknown: return m_str(IDS_LOADER_PSTATE_UNKNOWN);
				case ProcessState::Launcher: return m_str(IDS_LOADER_PSTATE_LAUNCHER);
			}
			return {};
		}

		[[nodiscard]] std::wstring ProcessLocation(const ProcessRow& row) const {
			if (row.Path.empty())
				return m_str(IDS_LOADER_PATH_UNKNOWN);
			return (row.Root.empty() ? row.Path : row.Root).wstring();
		}

		[[nodiscard]] std::wstring ProcessCellText(const ProcessRow& row, size_t column) const {
			// Basic shows the location, start time and application state only.
			if (!m_settings.Advanced)
				column += 3;
			switch (column) {
				case 0: return std::to_wstring(row.Pid);
				case 1: return row.Name;
				case 2: return row.Is64Bit ? L"x64" : L"x86";
				case 3: return ProcessLocation(row);
				case 4: return row.Started;
				case 5: return ProcessStateText(row);
			}
			return {};
		}

		[[nodiscard]] std::wstring InstallDetailsText(const InstallRow& row) const {
			std::vector<std::wstring> lines{row.Found.RootPath.wstring()};
			if (!m_settings.Advanced && row.Found.Sources)
				lines.emplace_back(m_str.Format(IDS_LOADER_DETAILS_FOUNDVIA, SourcesText(row.Found.Sources)));

			std::vector<std::wstring> notes;
			std::wstring others;
			for (const auto& dll : row.State.Dlls) {
				if (dll.Kind == Detector::ManagedDllKind::XivAlexander)
					continue;
				if (!others.empty())
					others += L", ";
				others += dll.Product.empty() ? dll.Name : std::format(L"{} ({})", dll.Name, dll.Product);
			}
			if (!others.empty())
				notes.emplace_back(m_str.Format(IDS_LOADER_DETAILS_THIRDPARTY, others));
			if (!row.State.ChainDlls.empty()) {
				std::wstring chained;
				for (const auto& path : row.State.ChainDlls) {
					if (!chained.empty())
						chained += L", ";
					chained += path.lexically_relative(row.GamePath()).wstring();
				}
				notes.emplace_back(m_str.Format(IDS_LOADER_DETAILS_CHAINLOADED, chained));
			}
			if (!row.Missing && row.Found.Info.Region == xivres::game_release_publisher::Unspecified)
				notes.emplace_back(m_str(IDS_LOADER_DETAILS_NOREGION));
			if (!row.Missing && !row.Writable && !m_isAdmin)
				notes.emplace_back(m_str(IDS_LOADER_DETAILS_NEEDSADMIN));
			if (std::ranges::any_of(m_processes, [&row](const auto& p) { return p.IsGame && IsUnder(p.Path, row.GamePath()); }))
				notes.emplace_back(m_str(IDS_LOADER_DETAILS_RUNNING));

			std::wstring text;
			for (const auto& line : lines)
				text += line + L"\n";
			for (size_t i = 0; i < notes.size(); ++i)
				text += (i ? L" " : L"") + notes[i];
			return text;
		}

		[[nodiscard]] std::wstring ProcessDetailsText(const ProcessRow& row) const {
			if (!m_settings.Advanced) {
				if (row.State == ProcessState::LoadedByInstall)
					return m_str(IDS_LOADER_SESSION_AUTO_NOTE);
				if (row.NeedsAdmin)
					return m_str(IDS_LOADER_SESSION_ADMIN_NOTE);
				return m_str(IDS_LOADER_SESSION_ONCE_NOTE);
			}
			std::wstring text = row.Path.empty() ? m_str(IDS_LOADER_PATH_UNKNOWN) : row.Path.wstring();
			if (row.IsGame && !row.Is64Bit)
				text += std::format(L"\n{}", m_str(IDS_LOADER_ERROR_LOAD_32BIT));
			else if (row.State == ProcessState::LoadedByInstall)
				text += std::format(L"\n{}", m_str(IDS_LOADER_PDETAILS_BYINSTALL));
			else if (row.NeedsAdmin)
				text += std::format(L"\n{}", m_str(IDS_LOADER_PDETAILS_NEEDSADMIN));
			return text;
		}

		void SetStatus(const std::wstring& text) {
			SetDlgItemTextW(m_hwnd, IDC_LOADER_STATUS, text.c_str());
		}

		// Selection and enable rules

		[[nodiscard]] const InstallRow* SelectedInstall() const {
			if (!m_installGrid)
				return nullptr;
			const auto row = m_installGrid->GetSelectedRow();
			return row && *row < m_installs.size() ? &m_installs[*row] : nullptr;
		}

		[[nodiscard]] const ProcessRow* SelectedProcess() const {
			if (!m_processGrid)
				return nullptr;
			const auto row = m_processGrid->GetSelectedRow();
			if (!row || *row >= m_visibleProcesses.size() || m_visibleProcesses[*row] >= m_processes.size())
				return nullptr;
			const auto& process = m_processes[m_visibleProcesses[*row]];
			if (!m_settings.Advanced) {
				const auto install = SelectedInstall();
				if (!install || install->Missing || !MatchesInstall(process, *install))
					return nullptr;
			}
			return &process;
		}

		[[nodiscard]] bool MatchesInstall(const ProcessRow& process, const InstallRow& install) const {
			return process.IsGame && ((!process.Root.empty() && SamePath(process.Root, install.Found.RootPath))
				|| (!process.Path.empty() && IsUnder(process.Path, install.GamePath())));
		}

		void RefreshProcessView(std::optional<DWORD> preferredPid = std::nullopt) {
			if (!m_processGrid)
				return;
			const auto selected = SelectedProcess();
			const auto pid = preferredPid.value_or(selected ? selected->Pid : 0);
			const auto install = SelectedInstall();
			m_visibleProcesses.clear();
			for (size_t i = 0; i < m_processes.size(); ++i)
				if (m_settings.Advanced || (install && !install->Missing && MatchesInstall(m_processes[i], *install)))
					m_visibleProcesses.push_back(i);
			m_processGrid->Refresh();
			m_processGrid->ClearSelection();
			for (size_t i = 0; i < m_visibleProcesses.size(); ++i) {
				if (m_processes[m_visibleProcesses[i]].Pid == pid) {
					m_processGrid->Select(i);
					return;
				}
			}
			// With several games, ask the user which one rather than choosing a process for them.
			if (m_visibleProcesses.size() == 1 || (m_settings.Advanced && !m_visibleProcesses.empty()))
				m_processGrid->Select(0);
		}

		[[nodiscard]] bool HasUnidentifiedGame() const {
			return std::ranges::any_of(m_processes, [](const ProcessRow& row) { return row.IsGame && row.Path.empty(); });
		}

		void UpdateBasicControls(bool canLaunch, bool canLoad) {
			const auto install = SelectedInstall();
			const auto process = SelectedProcess();
			const auto present = install && !install->Missing;
			const auto installed = present ? install->State.Installed() : nullptr;
			const auto needsInstall = present && (!installed || installed->IsOldStyleXivAlexander || VersionParts(installed->Version) < m_loaderVersion);
			EnableItem(IDC_LOADER_INSTALL, !m_busy && needsInstall);
			SetDlgItemTextW(m_hwnd, IDC_LOADER_INSTALL, m_str(installed ? (needsInstall ? IDS_LOADER_UPDATE_AUTO : IDS_LOADER_AUTO_READY) : IDS_LOADER_INSTALL_AUTO));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_AUTO_NOTE, m_str(IDS_LOADER_AUTO_NOTE));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_ONCE_NOTE, m_str(installed ? IDS_LOADER_ONCE_INSTALLED_NOTE : IDS_LOADER_ONCE_NOTE));

			const auto active = process && (process->State == ProcessState::Loaded || process->State == ProcessState::LoadedNoAccess || process->State == ProcessState::LoadedByInstall);
			const auto canStart = present && m_visibleProcesses.empty() && !HasUnidentifiedGame() && canLaunch;
			EnableItem(IDC_LOADER_APPLY_ONCE, !m_busy && present && (canLoad || canStart));
			SetDlgItemTextW(m_hwnd, IDC_LOADER_APPLY_ONCE, m_str(active ? IDS_LOADER_SESSION_ON : !m_visibleProcesses.empty() ? IDS_LOADER_APPLY_NOW : IDS_LOADER_START_ONCE));
			Button_SetElevationRequiredState(GetDlgItem(m_hwnd, IDC_LOADER_APPLY_ONCE), process && process->NeedsAdmin);

			UINT guidance = IDS_LOADER_GUIDE_SELECT;
			if (install && install->Missing)
				guidance = IDS_LOADER_GUIDE_MISSING;
			else if (present) {
				if (m_visibleProcesses.size() > 1 && !process)
					guidance = IDS_LOADER_GUIDE_CHOOSE_SESSION;
				else if (active)
					guidance = IDS_LOADER_GUIDE_ACTIVE;
				else if (process)
					guidance = IDS_LOADER_GUIDE_RUNNING;
				else if (HasUnidentifiedGame())
					guidance = IDS_LOADER_GUIDE_UNIDENTIFIED;
				else if (installed)
					guidance = IDS_LOADER_GUIDE_AUTO;
				else
					guidance = canLaunch ? IDS_LOADER_GUIDE_START : IDS_LOADER_GUIDE_NOBOOT;
			}
			SetDlgItemTextW(m_hwnd, IDC_LOADER_GUIDANCE, m_str(guidance));
		}

		void SelectInstallByRoot(const std::filesystem::path& root) {
			if (root.empty() || !m_installGrid)
				return;
			for (size_t i = 0; i < m_installs.size(); ++i) {
				if (SamePath(m_installs[i].Found.RootPath, root)) {
					m_installGrid->Select(i);
					return;
				}
			}
		}

		void EnableItem(int id, bool enable) {
			const auto hItem = GetDlgItem(m_hwnd, id);
			if (!enable && GetFocus() == hItem)
				SendMessageW(m_hwnd, WM_NEXTDLGCTL, 0, FALSE);
			EnableWindow(hItem, enable);
		}

		void UpdateControls() {
			const auto install = SelectedInstall();
			const auto process = SelectedProcess();
			const auto idle = !m_busy;

			const auto present = install && !install->Missing;
			const auto canLaunch = present && install->Found.Info.Region != xivres::game_release_publisher::Unspecified
				&& !install->Found.Info.BootApp.empty() && exists(install->Found.Info.BootApp);
			EnableItem(IDC_LOADER_INSTALL, idle && present);
			EnableItem(IDC_LOADER_UNINSTALL, idle && present && install->State.HasAnyXivAlexander());
			EnableItem(IDC_LOADER_LAUNCH, idle && canLaunch);
			EnableItem(IDC_LOADER_REMOVE, idle && install && install->UserAdded());
			EnableItem(IDC_LOADER_BROWSE, idle);
			EnableItem(IDC_LOADER_REFRESH, idle);
			EnableItem(IDC_LOADER_SHOWCONTROLWINDOW, idle && present);
			EnableItem(IDC_LOADER_ADVANCED, idle);
			EnableItem(IDC_LOADER_MORE, idle);
			const auto installShield = present && !install->Writable && !m_isAdmin;
			Button_SetElevationRequiredState(GetDlgItem(m_hwnd, IDC_LOADER_INSTALL), installShield);
			Button_SetElevationRequiredState(GetDlgItem(m_hwnd, IDC_LOADER_UNINSTALL), installShield);
			CheckDlgButton(m_hwnd, IDC_LOADER_SHOWCONTROLWINDOW, present && ReadShowControlWindow(*install) ? BST_CHECKED : BST_UNCHECKED);
			SetDlgItemTextW(m_hwnd, IDC_LOADER_INSTALL_DETAILS, install ? InstallDetailsText(*install).c_str() : L"");

			const auto game = process && process->IsGame;
			const auto canLoad = game && process->Is64Bit && (process->State == ProcessState::NotLoaded || process->State == ProcessState::Unknown);
			const auto canUnload = game && (process->State == ProcessState::Loaded || process->State == ProcessState::LoadedNoAccess || process->State == ProcessState::Unknown);
			EnableItem(IDC_LOADER_LOAD, idle && canLoad);
			EnableItem(IDC_LOADER_UNLOAD, idle && canUnload);
			Button_SetElevationRequiredState(GetDlgItem(m_hwnd, IDC_LOADER_LOAD), game && process->NeedsAdmin);
			Button_SetElevationRequiredState(GetDlgItem(m_hwnd, IDC_LOADER_UNLOAD), game && process->NeedsAdmin);
			SetDlgItemTextW(m_hwnd, IDC_LOADER_PROCESS_DETAILS, process ? ProcessDetailsText(*process).c_str() : L"");
			if (!m_settings.Advanced) {
				UpdateBasicControls(canLaunch, canLoad);
				UpdateBasicLayout();
			} else
				SetDlgItemTextW(m_hwnd, IDC_LOADER_INSTALL, m_str(IDS_LOADER_BUTTON_INSTALL));

			const auto progress = m_busy || m_scanning;
			const auto hProgress = GetDlgItem(m_hwnd, IDC_LOADER_PROGRESS);
			ShowWindow(hProgress, progress ? SW_SHOWNOACTIVATE : SW_HIDE);
			SendMessageW(hProgress, PBM_SETMARQUEE, progress, 0);
		}

		// The control window setting lives in the game's section of config.runtime.json.

		[[nodiscard]] std::string ConfigSectionKey(const InstallRow& row) const {
			return xivres::util::unicode::convert<std::string>((row.GamePath() / GameExecutable64NameW).wstring());
		}

		[[nodiscard]] bool ReadShowControlWindow(const InstallRow& row) const {
			auto config = XivAlexander::RuntimeConfigRepository({}, ConfigSectionKey(row));
			if (const auto path = m_paths.ConfigDir / L"config.runtime.json"; exists(path))
				config.Reload(path);
			return config.Ui.MainWindow.Show.Value();
		}

		void WriteShowControlWindow(const InstallRow& row, bool show) {
			create_directories(m_paths.ConfigDir);
			const auto path = m_paths.ConfigDir / L"config.runtime.json";
			auto config = XivAlexander::RuntimeConfigRepository(path, ConfigSectionKey(row));
			config.Reload();
			config.Ui.MainWindow.Show = show;
		}

		// Discovery

		void RecreateInstallGrid() {
			// The native list view caches empty markup, even when an empty list is refreshed.
			CreateInstallGrid();
			ArrangeTabOrder();
			ApplyZoom();
			ApplyTheme();
			Layout();
		}

		void StartDiscovery() {
			m_discovery = {};
			const auto generation = ++m_discoveryGeneration;
			{
				const auto lock = std::lock_guard(m_discoveredMtx);
				m_discovered.clear();
			}
			m_installs.clear();
			m_scanning = true;
			RecreateInstallGrid();
			SetStatus(m_str(IDS_LOADER_EMPTY_SCANNING));

			Detector::CollectOptions options{
				.UserAdded = m_settings.AddedPaths,
				.XivAlexanderConfigPath = m_paths.ConfigDir / L"config.runtime.json",
			};
			if (!m_args.m_runProgram.empty())
				options.CommandLine.emplace_back(m_args.m_runProgram);

			m_discovery = std::jthread([this, generation, options = std::move(options), isAdmin = m_isAdmin](std::stop_token stop) {
				const auto coUninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
				const auto push = [&](InstallRow row) {
					{
						const auto lock = std::lock_guard(m_discoveredMtx);
						m_discovered.emplace_back(std::move(row));
					}
					PostMessageW(m_hwnd, WmDiscovered, generation, 0);
				};

				for (const auto& path : options.UserAdded) {
					if (!Detector::FindRootStructurally(path))
						push(InstallRow{.Found = {.RootPath = path, .Sources = static_cast<uint32_t>(Detector::DiscoverySource::UserAdded)}, .Missing = true});
				}
				try {
					Detector::FindInstallations(options, [&](Detector::FoundInstallation found) {
						InstallRow row{.Found = std::move(found)};
						row.State = Detector::ProbeInstallState(row.GamePath());
						row.Writable = isAdmin || InstallUninstall::IsDirectoryWritable(row.GamePath());
						push(std::move(row));
					}, stop);
				} catch (...) {
					// whatever was found stays
				}
				PostMessageW(m_hwnd, WmDiscoveryDone, generation, 0);
				if (coUninit)
					CoUninitialize();
			});
		}

		void InsertInstallRow(InstallRow row) {
			const auto selected = SelectedInstall() ? SelectedInstall()->Found.RootPath : std::filesystem::path();
			const auto isCommandLine = row.Found.Sources & static_cast<uint32_t>(Detector::DiscoverySource::CommandLine);
			const auto root = row.Found.RootPath;
			const auto key = [](const InstallRow& r) {
				return std::tuple(r.Missing, r.Found.Info.Region == xivres::game_release_publisher::Unspecified, r.Found.Info.Region, r.Found.RootPath.wstring());
			};
			const auto it = std::ranges::upper_bound(m_installs, key(row), {}, key);
			m_installs.insert(it, std::move(row));
			m_installGrid->Refresh();
			SelectInstallByRoot(isCommandLine ? root : selected);
		}

		void OnDiscovered() {
			std::vector<InstallRow> rows;
			{
				const auto lock = std::lock_guard(m_discoveredMtx);
				rows = std::move(m_discovered);
				m_discovered.clear();
			}
			for (auto& row : rows)
				InsertInstallRow(std::move(row));
			UpdateControls();
		}

		void OnDiscoveryDone() {
			OnDiscovered();
			m_scanning = false;
			m_installGrid->Refresh();
			InvalidateRect(m_installGrid->Handle(), nullptr, TRUE);
			if (!SelectedInstall() && !m_installs.empty()) {
				// The installation of a running game, else the first one.
				auto index = static_cast<size_t>(0);
				for (const auto& process : m_processes) {
					const auto it = std::ranges::find_if(m_installs, [&process](const InstallRow& row) {
						return process.IsGame && !process.Root.empty() && !row.Missing && SamePath(row.Found.RootPath, process.Root);
					});
					if (it != m_installs.end()) {
						index = static_cast<size_t>(it - m_installs.begin());
						break;
					}
				}
				m_installGrid->Select(index);
			}
			if (m_installs.empty()) {
				RecreateInstallGrid();
				SetFocus(GetDlgItem(m_hwnd, IDC_LOADER_BROWSE));
			}
			if (!m_busy)
				SetStatus(m_str.Format(IDS_LOADER_STATUS_FOUND, std::ranges::count(m_installs, false, &InstallRow::Missing)));
			UpdateControls();
		}

		void RefreshProcesses() {
			auto rows = EnumerateProcesses(m_settings.Advanced, m_isAdmin);
			if (rows == m_processes)
				return;
			const auto selectedPid = SelectedProcess() ? SelectedProcess()->Pid : 0;
			m_processes = std::move(rows);
			RefreshProcessView(selectedPid);
			UpdateControls();
		}

		// Operations: one at a time, on a worker; the result comes back as a function to run here.

		void RunOperation(const std::wstring& status, std::function<std::function<void()>()> work) {
			if (m_busy)
				return;
			m_busy = true;
			SetStatus(status);
			UpdateControls();
			m_operation = std::thread([this, work = std::move(work)] {
				const auto coUninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
				std::function<void()> result;
				try {
					result = work();
				} catch (const Utils::Win32::CancelledError&) {
					result = [this] { SetStatus(m_str(IDS_LOADER_STATUS_ELEVATEDFAILED)); };
				} catch (const std::exception& e) {
					result = [this, message = xivres::util::unicode::convert<std::wstring>(e.what())] {
						const auto text = m_str.Format(IDS_LOADER_STATUS_FAILED, message);
						SetStatus(text);
						MessageBoxW(m_hwnd, text.c_str(), GetGenericMessageBoxTitle(), MB_OK | MB_ICONERROR);
					};
				}
				{
					const auto lock = std::lock_guard(m_operationMtx);
					m_operationResult = std::move(result);
				}
				if (coUninit)
					CoUninitialize();
				PostMessageW(m_hwnd, WmOperationDone, 0, 0);
			});
		}

		void OnOperationDone() {
			if (m_operation.joinable())
				m_operation.join();
			std::function<void()> result;
			{
				const auto lock = std::lock_guard(m_operationMtx);
				result = std::move(m_operationResult);
			}
			m_busy = false;
			if (result)
				result();
			RefreshProcesses();
			UpdateControls();
		}

		void UpdateInstallRow(const std::filesystem::path& root, const Detector::InstallState& state, bool writable) {
			for (auto& row : m_installs) {
				if (SamePath(row.Found.RootPath, root)) {
					row.State = state;
					row.Writable = writable;
				}
			}
			m_installGrid->Refresh();
		}

		void Install(InstallMode mode) {
			const auto row = SelectedInstall();
			if (!row || row->Missing)
				return;
			const auto root = row->Found.RootPath;
			const auto gamePath = row->GamePath();
			RunOperation(m_str.Format(IDS_LOADER_STATUS_INSTALLING, root.wstring()), [this, root, gamePath, mode]() -> std::function<void()> {
				if (!InstallUninstall::Install(gamePath, mode, m_paths, true))
					return [this] { SetStatus(m_str(IDS_LOADER_STATUS_CANCELLED)); };
				auto state = Detector::ProbeInstallState(gamePath);
				const auto writable = m_isAdmin || InstallUninstall::IsDirectoryWritable(gamePath);
				return [this, root, state = std::move(state), writable] {
					UpdateInstallRow(root, state, writable);
					SetStatus(m_str.Format(m_settings.Advanced ? IDS_LOADER_STATUS_INSTALLED : IDS_LOADER_DONE_INSTALL, root.wstring()));
				};
			});
		}

		void Uninstall() {
			const auto row = SelectedInstall();
			if (!row || row->Missing)
				return;
			const auto root = row->Found.RootPath;
			const auto gamePath = row->GamePath();
			const auto region = row->Found.Info.Region;
			std::vector<std::pair<std::filesystem::path, xivres::game_release_publisher>> others;
			for (const auto& other : m_installs) {
				if (!other.Missing && !SamePath(other.Found.RootPath, root))
					others.emplace_back(other.GamePath(), other.Found.Info.Region);
			}

			RunOperation(m_str.Format(IDS_LOADER_STATUS_UNINSTALLING, root.wstring()), [this, root, gamePath, region, others]() -> std::function<void()> {
				if (!InstallUninstall::Uninstall(gamePath, m_paths, true))
					return [this] { SetStatus(m_str(IDS_LOADER_STATUS_CANCELLED)); };
				auto state = Detector::ProbeInstallState(gamePath);
				const auto writable = m_isAdmin || InstallUninstall::IsDirectoryWritable(gamePath);

				// What remains installed decides what cleanup is offered.
				auto anyOther = false, sameRegion = false;
				for (const auto& [otherGamePath, otherRegion] : others) {
					if (Detector::ProbeInstallState(otherGamePath).HasAnyXivAlexander()) {
						anyOther = true;
						sameRegion |= otherRegion == region;
					}
				}
				std::optional<LeftoverCleanup::Scope> scope;
				if (!anyOther || !sameRegion) {
					scope = LeftoverCleanup::Scope{
						.ConfigDir = m_paths.ConfigDir,
						.ProgramDir = m_paths.ProgramDir,
						.LastInstallation = !anyOther,
						.SessionsPublisher = region,
						.IncludeUnknownSessions = !anyOther,
					};
					if (anyOther && (region == xivres::game_release_publisher::Unspecified || !LeftoverCleanup::CountSessions(*scope)))
						scope.reset();
				}
				return [this, root, state = std::move(state), writable, scope] {
					UpdateInstallRow(root, state, writable);
					SetStatus(m_str.Format(m_settings.Advanced ? IDS_LOADER_STATUS_UNINSTALLED : IDS_LOADER_DONE_UNINSTALL, root.wstring()));
					if (scope)
						OfferCleanup(*scope);
				};
			});
		}

		void OfferCleanup(const LeftoverCleanup::Scope& scope) {
			const auto items = LeftoverCleanup::ShowDialog(m_hwnd, m_str.LangId(), m_theme.Dark(), scope);
			if (!items)
				return;
			RunOperation(m_str(IDS_LOADER_STATUS_CLEANING), [this, scope, items]() -> std::function<void()> {
				const auto result = LeftoverCleanup::Perform(scope, items);
				return [this, result] {
					if (!result.Error.empty())
						SetStatus(m_str.Format(IDS_LOADER_STATUS_FAILED, result.Error));
					else if (result.EntriesLeft)
						SetStatus(m_str.Format(IDS_LOADER_STATUS_CLEANUP_PARTIAL, result.EntriesLeft));
					else
						SetStatus(m_str(IDS_LOADER_STATUS_CLEANUP_DONE));
				};
			});
		}

		void Launch(bool withXivAlexander) {
			const auto row = SelectedInstall();
			if (!row || row->Missing || row->Found.Info.BootApp.empty() || !exists(row->Found.Info.BootApp))
				return;
			const auto boot = row->Found.Info.BootApp;
			// Square Enix's boot program updates itself and asks for elevation on its own.
			const auto elevateMode = row->Found.Info.Region == xivres::game_release_publisher::SquareEnix
				? Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated
				: Utils::Win32::RunProgramParams::NoElevationIfDenied;
			RunOperation(m_str.Format(IDS_LOADER_STATUS_LAUNCHING, boot.filename().wstring()), [this, boot, elevateMode, withXivAlexander]() -> std::function<void()> {
				Utils::Win32::Process process;
				if (withXivAlexander) {
					process = Utils::Win32::RunProgram({
						.args = Utils::Win32::ReverseCommandLineToArgv({
							std::wstring(L"-a"), xivres::util::unicode::convert<std::wstring>(LoaderActionToString(LoaderAction::Launcher)),
							std::wstring(L"-l"), std::wstring(L"select"),
							boot.wstring(),
						}),
						.elevateMode = elevateMode,
					});
				} else {
					SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", L"1");
					const auto restore = xivres::util::on_dtor([] { SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr); });
					process = Utils::Win32::RunProgram({
						.path = boot,
						.dir = boot.parent_path(),
						.elevateMode = elevateMode,
					});
				}
				if (!process)
					return [this] { SetStatus(m_str(IDS_LOADER_STATUS_CANCELLED)); };
				return [this, boot] { SetStatus(m_str.Format(IDS_LOADER_STATUS_LAUNCHED, boot.filename().wstring())); };
			});
		}

		void LoadOrUnload(bool load) {
			const auto row = SelectedProcess();
			if (!row || !row->IsGame)
				return;
			const auto pid = row->Pid;
			const auto needsAdmin = row->NeedsAdmin;
			const auto statusId = m_settings.Advanced ? (load ? IDS_LOADER_STATUS_LOADING : IDS_LOADER_STATUS_UNLOADING)
				: (load ? IDS_LOADER_APPLYING : IDS_LOADER_STOPPING);
			RunOperation(m_str.Format(statusId, pid), [this, pid, load, needsAdmin]() -> std::function<void()> {
				if (needsAdmin) {
					// The elevated loader reports its own errors.
					const auto process = Utils::Win32::RunProgram({
						.args = std::format(L"--disable-runas -a {} {}", xivres::util::unicode::convert<std::wstring>(LoaderActionToString(load ? LoaderAction::Load : LoaderAction::Unload)), pid),
						.elevateMode = Utils::Win32::RunProgramParams::Force,
					});
					if (!process)
						return [this] { SetStatus(m_str(IDS_LOADER_STATUS_CANCELLED)); };
					if (process.WaitAndGetExitCode() != 0)
						throw Utils::Win32::CancelledError();
				} else {
					auto loadUnload = XivAlexander::LoaderApp::Actions::LoadUnload(m_args);
					if (load)
						loadUnload.Load(pid);
					else
						loadUnload.Unload(pid);
				}
				return [this, pid, load] {
					const auto id = m_settings.Advanced ? (load ? IDS_LOADER_STATUS_LOADED : IDS_LOADER_STATUS_UNLOADED)
						: (load ? IDS_LOADER_DONE_APPLY : IDS_LOADER_DONE_STOP);
					SetStatus(m_str.Format(id, pid));
				};
			});
		}

		void ApplyOnce() {
			// Recheck immediately: the game may have closed since the last timer tick.
			RefreshProcesses();
			RefreshProcessView();
			UpdateControls();
			if (!IsWindowEnabled(GetDlgItem(m_hwnd, IDC_LOADER_APPLY_ONCE)))
				return;
			if (SelectedProcess())
				LoadOrUnload(true);
			else
				Launch(true);
		}

		// Installations added by hand

		void AddUserPath(const std::filesystem::path& picked, bool folder) {
			std::vector<std::filesystem::path> roots;
			if (const auto root = Detector::FindRootStructurally(picked))
				roots.emplace_back(*root);
			else if (folder)
				roots = Detector::FindRootsBelow(picked, 2);
			if (roots.empty()) {
				SetStatus(m_str.Format(IDS_LOADER_BROWSE_NOTFOUND, picked.wstring()));
				return;
			}

			std::vector<std::filesystem::path> added;
			for (const auto& root : roots) {
				const auto id = Detector::GetDirectoryId(root / L"game");
				const auto existing = std::ranges::find_if(m_installs, [&](const InstallRow& row) {
					return !row.Missing && row.Found.IsSameAs(Detector::FoundInstallation{.RootPath = root, .GameDirId = id});
				});
				if (existing != m_installs.end()) {
					m_installGrid->Select(static_cast<size_t>(existing - m_installs.begin()));
					SetStatus(m_str.Format(IDS_LOADER_BROWSE_EXISTS, root.wstring()));
					continue;
				}
				if (std::ranges::none_of(m_settings.AddedPaths, [&root](const auto& p) { return SamePath(p, root); }))
					m_settings.AddedPaths.emplace_back(root);
				added.emplace_back(root);
			}
			if (added.empty())
				return;
			SaveSettings();

			RunOperation(m_str.Format(IDS_LOADER_BROWSE_ADDING, added.front().wstring()), [this, added, isAdmin = m_isAdmin]() -> std::function<void()> {
				std::vector<InstallRow> rows;
				for (const auto& root : added) {
					InstallRow row{.Found = Detector::ResolveInstallation(root)};
					row.Found.Sources = static_cast<uint32_t>(Detector::DiscoverySource::UserAdded);
					row.Found.Details.emplace_back(Detector::DiscoverySource::UserAdded, root.wstring());
					row.State = Detector::ProbeInstallState(row.GamePath());
					row.Writable = isAdmin || InstallUninstall::IsDirectoryWritable(row.GamePath());
					rows.emplace_back(std::move(row));
				}
				return [this, rows = std::move(rows)]() mutable {
					// Drop saved rows that were missing and are now found.
					for (const auto& row : rows) {
						std::erase_if(m_installs, [&row](const InstallRow& r) { return r.Missing && SamePath(r.Found.RootPath, row.Found.RootPath); });
					}
					auto last = std::filesystem::path();
					for (auto& row : rows) {
						last = row.Found.RootPath;
						InsertInstallRow(std::move(row));
					}
					SelectInstallByRoot(last);
					SetStatus(m_str.Format(IDS_LOADER_BROWSE_ADDED, last.wstring()));
				};
			});
		}

		void Browse(bool folder) {
			auto startIn = std::filesystem::path();
			if (const auto row = SelectedInstall())
				startIn = row->Found.RootPath.parent_path();
			std::filesystem::path picked;
			try {
				picked = PickPath(m_hwnd, m_str, folder, startIn);
			} catch (const Utils::Win32::CancelledError&) {
				return;
			}
			if (!picked.empty())
				AddUserPath(picked, folder);
		}

		void RemoveSelectedFromList() {
			const auto row = SelectedInstall();
			if (!row || !row->UserAdded())
				return;
			const auto root = row->Found.RootPath;
			std::erase_if(m_settings.AddedPaths, [&root](const auto& p) {
				if (SamePath(p, root))
					return true;
				const auto found = Detector::FindRootStructurally(p);
				return found && SamePath(*found, root);
			});
			SaveSettings();

			const auto index = static_cast<size_t>(row - m_installs.data());
			auto& sources = m_installs[index].Found.Sources;
			sources &= ~static_cast<uint32_t>(Detector::DiscoverySource::UserAdded);
			if (!sources || m_installs[index].Missing)
				m_installs.erase(m_installs.begin() + static_cast<ptrdiff_t>(index));
			m_installGrid->Refresh();
			if (!m_installs.empty())
				m_installGrid->Select(std::min(index, m_installs.size() - 1));
			UpdateControls();
		}

		void SaveSettings() {
			try {
				m_settings.Save(m_paths.ConfigDir);
			} catch (const std::exception& e) {
				SetStatus(m_str.Format(IDS_LOADER_STATUS_FAILED, xivres::util::unicode::convert<std::wstring>(e.what())));
			}
		}

		// Menus

		void ShowInstallContextMenu(size_t index, POINT pt) {
			if (index >= m_installs.size() || m_busy)
				return;
			const auto& row = m_installs[index];
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
			const auto present = !row.Missing;
			AppendMenuW(hMenu, MF_STRING | (present ? 0 : MF_GRAYED), CmdOpenFolder, m_str(IDS_LOADER_MENU_OPENFOLDER));
			AppendMenuW(hMenu, MF_STRING, CmdCopyPath, m_str(IDS_LOADER_MENU_COPYPATH));
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			const auto canLaunch = IsWindowEnabled(GetDlgItem(m_hwnd, IDC_LOADER_LAUNCH));
			AppendMenuW(hMenu, MF_STRING | (canLaunch ? 0 : MF_GRAYED), CmdLaunchWith, m_str(IDS_LOADER_MENU_LAUNCH_WITH));
			AppendMenuW(hMenu, MF_STRING | (canLaunch ? 0 : MF_GRAYED), CmdLaunchWithout, m_str(IDS_LOADER_MENU_LAUNCH_WITHOUT));
			if (row.UserAdded()) {
				AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
				AppendMenuW(hMenu, MF_STRING, CmdRemove, m_str(IDS_LOADER_MENU_REMOVE));
			}
			const auto command = static_cast<UINT>(TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hwnd, nullptr));
			if (command)
				OnCommand(command, 0);
		}

		void ShowMoreMenu() {
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });

			const auto hLanguage = CreatePopupMenu();
			const auto language = m_settings.UiLanguage.value_or(XivAlexander::Language::SystemDefault);
			for (const auto& [command, value, id] : std::initializer_list<std::tuple<UINT, XivAlexander::Language, UINT>>{
					{CmdLanguageSystem, XivAlexander::Language::SystemDefault, IDS_LOADER_MENU_LANGUAGE_SYSTEM},
					{CmdLanguageEnglish, XivAlexander::Language::English, IDS_LOADER_MENU_LANGUAGE_ENGLISH},
					{CmdLanguageKorean, XivAlexander::Language::Korean, IDS_LOADER_MENU_LANGUAGE_KOREAN},
					{CmdLanguageJapanese, XivAlexander::Language::Japanese, IDS_LOADER_MENU_LANGUAGE_JAPANESE},
				})
				AppendMenuW(hLanguage, MF_STRING | (m_settings.UiLanguage && language == value ? MF_CHECKED : 0) | (!m_settings.UiLanguage && value == XivAlexander::Language::SystemDefault ? MF_CHECKED : 0), command, m_str(id));
			AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hLanguage), m_str(IDS_SETTINGS_UI_LANGUAGE));

			const auto hTheme = CreatePopupMenu();
			const auto theme = m_settings.EffectiveThemeMode(m_paths.ConfigDir);
			for (const auto& [command, value, id] : std::initializer_list<std::tuple<UINT, XivAlexander::ThemeMode, UINT>>{
					{CmdThemeSystem, XivAlexander::ThemeMode::System, IDS_LOADER_MENU_THEME_SYSTEM},
					{CmdThemeLight, XivAlexander::ThemeMode::Light, IDS_LOADER_MENU_THEME_LIGHT},
					{CmdThemeDark, XivAlexander::ThemeMode::Dark, IDS_LOADER_MENU_THEME_DARK},
				})
				AppendMenuW(hTheme, MF_STRING | (theme == value ? MF_CHECKED : 0), command, m_str(id));
			AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hTheme), m_str(IDS_SETTINGS_UI_THEMEMODE));

			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(hMenu, MF_STRING, CmdCheckUpdates, m_str(IDS_LOADER_MENU_CHECKUPDATES));
			AppendMenuW(hMenu, MF_STRING, CmdHomepage, m_str(IDS_LOADER_MENU_HOMEPAGE));
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(hMenu, MF_STRING, CmdOpenConfigDir, m_str(IDS_LOADER_MENU_OPENCONFIGDIR));
			AppendMenuW(hMenu, MF_STRING, CmdOpenProgramDir, m_str(IDS_LOADER_MENU_OPENPROGRAMDIR));
			const auto anyInstalled = m_scanning || std::ranges::any_of(m_installs, [](const auto& row) { return row.State.HasAnyXivAlexander(); });
			AppendMenuW(hMenu, MF_STRING | (anyInstalled ? MF_GRAYED : 0), CmdCleanup, m_str(IDS_LOADER_MENU_CLEANUP));

			if (const auto command = Ui::TrackMenuUnder(GetDlgItem(m_hwnd, IDC_LOADER_MORE), hMenu))
				OnCommand(command, 0);
		}

		void OnDropDown(UINT id) {
			if (m_busy)
				return;
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
			switch (id) {
				case IDC_LOADER_INSTALL:
					if (!m_settings.Advanced)
						return;
					AppendMenuW(hMenu, MF_STRING, CmdInstallD3d11, m_str(IDS_LOADER_MENU_INSTALL_D3D11));
					AppendMenuW(hMenu, MF_STRING, CmdInstallDinput8, m_str(IDS_LOADER_MENU_INSTALL_DINPUT8));
					AppendMenuW(hMenu, MF_STRING, CmdInstallDxgi, m_str(IDS_LOADER_MENU_INSTALL_DXGI));
					SetMenuDefaultItem(hMenu, CmdInstallD3d11, FALSE);
					break;
				case IDC_LOADER_LAUNCH:
					AppendMenuW(hMenu, MF_STRING, CmdLaunchWith, m_str(IDS_LOADER_MENU_LAUNCH_WITH));
					AppendMenuW(hMenu, MF_STRING, CmdLaunchWithout, m_str(IDS_LOADER_MENU_LAUNCH_WITHOUT));
					SetMenuDefaultItem(hMenu, CmdLaunchWith, FALSE);
					break;
				case IDC_LOADER_BROWSE:
					AppendMenuW(hMenu, MF_STRING, CmdBrowseFile, m_str(IDS_LOADER_MENU_PICKFILE));
					AppendMenuW(hMenu, MF_STRING, CmdBrowseFolder, m_str(IDS_LOADER_MENU_PICKFOLDER));
					SetMenuDefaultItem(hMenu, m_settings.Advanced ? CmdBrowseFile : CmdBrowseFolder, FALSE);
					break;
				case IDC_LOADER_MORE:
					ShowMoreMenu();
					return;
				default:
					return;
			}
			if (const auto command = Ui::TrackMenuUnder(GetDlgItem(m_hwnd, static_cast<int>(id)), hMenu))
				OnCommand(command, BN_CLICKED);
		}

		void SetLanguage(XivAlexander::Language language) {
			if (language == XivAlexander::Language::SystemDefault)
				m_settings.UiLanguage.reset();
			else
				m_settings.UiLanguage = language;
			SaveSettings();
			if (m_busy)
				return;
			m_outcome = Outcome::Restart;
			DestroyWindow(m_hwnd);
		}

		void SetTheme(XivAlexander::ThemeMode theme) {
			m_settings.UiThemeMode = theme;
			SaveSettings();
			ApplyTheme();
		}

		void OpenFolder(const std::filesystem::path& path) {
			try {
				create_directories(path);
				Utils::Win32::ShellExecutePathOrThrow(path, m_hwnd);
			} catch (const std::exception& e) {
				SetStatus(m_str.Format(IDS_LOADER_STATUS_FAILED, xivres::util::unicode::convert<std::wstring>(e.what())));
			}
		}

		void CopyToClipboard(const std::wstring& text) {
			if (!OpenClipboard(m_hwnd))
				return;
			const auto closeClipboard = xivres::util::on_dtor([] { CloseClipboard(); });
			EmptyClipboard();
			const auto hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
			if (!hMem)
				return;
			memcpy(GlobalLock(hMem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
			GlobalUnlock(hMem);
			if (!SetClipboardData(CF_UNICODETEXT, hMem))
				GlobalFree(hMem);
		}

		void OnCommand(UINT id, UINT code) {
			if (code != BN_CLICKED && code != 0)
				return;
			if (m_busy && id != IDCANCEL)
				return;

			switch (id) {
				case IDCANCEL:
					if (m_busy) {
						SetStatus(m_str(IDS_LOADER_STATUS_BUSY));
						return;
					}
					DestroyWindow(m_hwnd);
					return;

				case IDC_LOADER_INSTALL:
					if (!IsWindowEnabled(GetDlgItem(m_hwnd, IDC_LOADER_INSTALL)))
						return;
					// Updating from Basic preserves the previously chosen installation method.
					if (!m_settings.Advanced) {
						if (const auto row = SelectedInstall(); row && row->State.Installed()) {
							const auto& slot = row->State.Installed()->Name;
							Install(EqualsI(slot, L"dinput8.dll") ? InstallMode::DInput8 : EqualsI(slot, L"dxgi.dll") ? InstallMode::Dxgi : InstallMode::D3D);
							return;
						}
					}
					Install(InstallMode::D3D);
					return;
				case IDC_LOADER_APPLY_ONCE:
					ApplyOnce();
					return;
				case CmdInstallD3d11:
					Install(InstallMode::D3D);
					return;
				case CmdInstallDinput8:
					Install(InstallMode::DInput8);
					return;
				case CmdInstallDxgi:
					Install(InstallMode::Dxgi);
					return;

				case IDC_LOADER_UNINSTALL:
					Uninstall();
					return;

				case IDC_LOADER_LAUNCH:
				case CmdLaunchWith:
					Launch(true);
					return;
				case CmdLaunchWithout:
					Launch(false);
					return;

				case IDC_LOADER_BROWSE:
					Browse(!m_settings.Advanced);
					return;
				case CmdBrowseFolder:
					Browse(true);
					return;
				case CmdBrowseFile:
					Browse(false);
					return;

				case IDC_LOADER_REMOVE:
				case CmdRemove:
					RemoveSelectedFromList();
					return;

				case IDC_LOADER_REFRESH:
					StartDiscovery();
					RefreshProcesses();
					UpdateControls();
					return;

				case IDC_LOADER_SHOWCONTROLWINDOW:
					if (const auto row = SelectedInstall(); row && !row->Missing)
						WriteShowControlWindow(*row, IsDlgButtonChecked(m_hwnd, IDC_LOADER_SHOWCONTROLWINDOW) == BST_CHECKED);
					return;

				case IDC_LOADER_LOAD:
					LoadOrUnload(true);
					return;
				case IDC_LOADER_UNLOAD:
					LoadOrUnload(false);
					return;

				case IDC_LOADER_ADVANCED:
					m_settings.Advanced = IsDlgButtonChecked(m_hwnd, IDC_LOADER_ADVANCED) == BST_CHECKED;
					SaveSettings();
					SendMessageW(m_hwnd, WM_SETREDRAW, FALSE, 0);
					SetButtonSplit(IDC_LOADER_INSTALL, m_settings.Advanced);
					ApplyMode();
					CreateInstallGrid();
					m_visibleProcesses.clear();
					m_processes.clear();
					CreateProcessGrid();
					ArrangeTabOrder();
					ApplyTheme();
					ApplyZoom();
					UpdateMinTrackSize();
					{
						RECT rc{};
						GetWindowRect(m_hwnd, &rc);
						SetWindowPos(m_hwnd, nullptr, 0, 0, std::max<LONG>(rc.right - rc.left, m_minTrackSize.cx), std::max<LONG>(rc.bottom - rc.top, m_minTrackSize.cy), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
					}
					Layout();
					RefreshProcesses();
					UpdateControls();
					SendMessageW(m_hwnd, WM_SETREDRAW, TRUE, 0);
					RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
					return;

				case IDC_LOADER_MORE:
					ShowMoreMenu();
					return;

				case CmdOpenFolder:
					if (const auto row = SelectedInstall())
						OpenFolder(row->Found.RootPath);
					return;
				case CmdCopyPath:
					if (const auto row = SelectedInstall())
						CopyToClipboard(row->Found.RootPath.wstring());
					return;

				case CmdLanguageSystem:
					SetLanguage(XivAlexander::Language::SystemDefault);
					return;
				case CmdLanguageEnglish:
					SetLanguage(XivAlexander::Language::English);
					return;
				case CmdLanguageKorean:
					SetLanguage(XivAlexander::Language::Korean);
					return;
				case CmdLanguageJapanese:
					SetLanguage(XivAlexander::Language::Japanese);
					return;
				case CmdThemeSystem:
					SetTheme(XivAlexander::ThemeMode::System);
					return;
				case CmdThemeLight:
					SetTheme(XivAlexander::ThemeMode::Light);
					return;
				case CmdThemeDark:
					SetTheme(XivAlexander::ThemeMode::Dark);
					return;

				case CmdCheckUpdates:
					RunOperation(m_str(IDS_LOADER_STATUS_CHECKINGUPDATES), [this]() -> std::function<void()> {
						XivAlexander::LoaderApp::Actions::Update(m_args).CheckForUpdates(m_args.m_targetProcessHandles, true);
						return [this] { SetStatus({}); };
					});
					return;
				case CmdHomepage:
					try {
						Utils::Win32::ShellExecutePathOrThrow(m_str(IDS_URL_HOMEPAGE), m_hwnd);
					} catch (const std::exception& e) {
						SetStatus(m_str.Format(IDS_LOADER_STATUS_FAILED, xivres::util::unicode::convert<std::wstring>(e.what())));
					}
					return;
				case CmdOpenConfigDir:
					OpenFolder(m_paths.ConfigDir);
					return;
				case CmdOpenProgramDir:
					OpenFolder(m_paths.ProgramDir);
					return;
				case CmdCleanup:
					OfferCleanup(LeftoverCleanup::Scope{
						.ConfigDir = m_paths.ConfigDir,
						.ProgramDir = m_paths.ProgramDir,
					});
					return;
			}
		}
	};

	std::set<std::string> SplitItems(const std::string& list) {
		std::set<std::string> items;
		for (auto& item : xivres::util::split(list, std::string(","))) {
			std::ranges::transform(item, item.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			if (!item.empty())
				items.insert(item);
		}
		return items;
	}
}

XivAlexander::LoaderApp::Actions::Interactive::Interactive(const Arguments& args)
	: m_args(args) {
}

int XivAlexander::LoaderApp::Actions::Interactive::Run() {
	// Shell dialogs want a single-threaded apartment; the loader's main thread is multithreaded.
	auto result = 0;
	std::exception_ptr error;
	std::thread([this, &result, &error] {
		const auto coUninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
		try {
			const auto paths = InstallUninstall::Paths::FromArguments(m_args);
			if (!m_args.m_testCleanupItems.empty()) {
				// Test hook: removes leftovers in explicitly given folders without any window; never login sessions.
				if (m_args.m_configDir.empty() || m_args.m_programDir.empty())
					throw std::invalid_argument("--test-cleanup needs --config-dir and --program-dir");
				const auto names = SplitItems(m_args.m_testCleanupItems);
				uint32_t items = 0;
				items |= names.contains("caches") ? LeftoverCleanup::Caches : 0;
				items |= names.contains("program") ? LeftoverCleanup::ProgramFiles : 0;
				items |= names.contains("settings") ? LeftoverCleanup::Settings : 0;
				items |= names.contains("user") ? LeftoverCleanup::UserContent : 0;
				const auto cleanup = LeftoverCleanup::Perform({.ConfigDir = paths.ConfigDir, .ProgramDir = paths.ProgramDir}, items);
				ReportError(true, std::format(L"left={} error={}", cleanup.EntriesLeft, cleanup.Error));
				result = cleanup.EntriesLeft || !cleanup.Error.empty() ? 1 : 0;
			} else if (m_args.m_showCleanupDialogTest) {
				// Test hook: the cleanup dialog over the given folders; nothing is removed whatever is chosen.
				const auto settings = LoaderSettings::Load(paths.ConfigDir);
				result = static_cast<int>(LeftoverCleanup::ShowDialog(nullptr,
					LangIdOf(settings.EffectiveLanguage(paths.ConfigDir)),
					Window::IsDarkModeEnabled(settings.EffectiveThemeMode(paths.ConfigDir)),
					{.ConfigDir = paths.ConfigDir, .ProgramDir = paths.ProgramDir, .SessionsPublisher = xivres::game_release_publisher::SquareEnix, .TestOnly = true}));
			} else {
				while (LoaderDialog(m_args).Run() == LoaderDialog::Outcome::Restart) {
					// the language changed; open again in the new one
				}
			}
		} catch (...) {
			error = std::current_exception();
		}
		if (coUninit)
			CoUninitialize();
	}).join();
	if (error)
		std::rethrow_exception(error);
	return result;
}
