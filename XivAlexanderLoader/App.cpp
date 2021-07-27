#include "pch.h"
#include "resource.h"

const auto MsgboxTitle = L"XivAlexander Loader";

static
void CheckPackageVersions() {
	const auto dir = Utils::Win32::Process::Current().PathOf().parent_path();
	std::vector<std::pair<std::string, std::string>> modules;
	try {
		modules = {
			Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexLoader32NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexLoader64NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexDll32NameW),
			Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexDll64NameW),
		};
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() == ERROR_FILE_NOT_FOUND)
			throw std::runtime_error(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_MISSING_FILES) + 1));
	}
	for (size_t i = 1; i < modules.size(); ++i) {
		if (modules[0].first != modules[i].first || modules[0].second != modules[i].second)
			throw std::runtime_error(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_INCONSISTENT_FILES) + 1));
	}
}

template <>
std::string argparse::details::repr(XivAlexDll::LoaderAction const& val) {
	return LoaderActionToString(val);
}

enum class LauncherType : int {
	Auto,
	Select,
	International,
	Korean,
	Chinese,
	Count_,  // for internal use only
};

template <>
std::string argparse::details::repr(LauncherType const& val) {
	switch (val) {
		case LauncherType::Auto: return "auto";
		case LauncherType::Select: return "select";
		case LauncherType::International: return "international";
		case LauncherType::Korean: return "korean";
		case LauncherType::Chinese: return "chinese";
	}
	return std::format("({})", static_cast<int>(val));
}

template <>
std::string argparse::details::repr(XivAlex::GameRegion const& val) {
	switch (val) {
		case XivAlex::GameRegion::International: return Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_CLIENT_INTERNATIONAL) + 1);
		case XivAlex::GameRegion::Korean: return Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_CLIENT_KOREAN) + 1);
		case XivAlex::GameRegion::Chinese: return Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_CLIENT_CHINESE) + 1);
	}
	return std::format("({})", static_cast<int>(val));
}

LauncherType ParseLauncherType(const std::string& val_) {
	auto val = Utils::FromUtf8(val_);
	CharLowerW(&val[0]);
	for (size_t i = 0; i < static_cast<size_t>(LauncherType::Count_); ++i) {
		auto compare = Utils::FromUtf8(argparse::details::repr(static_cast<LauncherType>(i)));
		CharLowerW(&compare[0]);

		auto equal = true;
		for (size_t j = 0; equal && j < val.length() && j < compare.length(); ++j)
			equal = val[j] == compare[j];
		if (equal)
			return static_cast<LauncherType>(i);
	}
	if (val[0] == L'e' || val[0] == L'd' || val[0] == L'g' || val[0] == L'f' || val[0] == L'j')
		return LauncherType::International;

	throw std::runtime_error(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_INVALID_LAUNCHER_TYPE) + 1));
}

class XivAlexanderLoaderParameter {
public:
	argparse::ArgumentParser argp;

	XivAlexDll::LoaderAction m_action = XivAlexDll::LoaderAction::Auto;
	LauncherType m_launcherType = LauncherType::Auto;
	bool m_quiet = false;
	bool m_help = false;
	bool m_disableAutoRunAs = true;
	std::set<DWORD> m_targetPids{};
	std::vector<Utils::Win32::Process> m_targetProcessHandles{};
	std::set<std::wstring> m_targetSuffix{};
	std::wstring m_runProgram;
	std::wstring m_runProgramArgs;
	bool m_debugUpdate = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);

	XivAlexanderLoaderParameter()
		: argp("XivAlexanderLoader") {
		// SetThreadUILanguage(MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN));
		// SetThreadUILanguage(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN));

		argp.add_argument("-a", "--action")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_ACTION) + 1))
			.required()
			.nargs(1)
			.default_value(XivAlexDll::LoaderAction::Auto)
			.action([](const std::string& val) { return XivAlexDll::ParseLoaderAction(val); });
		argp.add_argument("-l", "--launcher")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_LAUNCHER) + 1))
			.required()
			.nargs(1)
			.default_value(LauncherType::Auto)
			.action([](const std::string& val) { return ParseLauncherType(val); });
		argp.add_argument("-q", "--quiet")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_QUIET) + 1))
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("-d", "--disable-runas")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_DISABLE_RUNAS) + 1))
			.default_value(false)
			.implicit_value(true);

		// internal use
		argp.add_argument("--handle-instead-of-pid")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_INTERNAL_USE_ONLY) + 1))
			.required()
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("--wait-process")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_INTERNAL_USE_ONLY) + 1))
			.required()
			.nargs(1)
			.default_value(Utils::Win32::Process())
			.action([](const std::string& val) {
				return Utils::Win32::Process(reinterpret_cast<HANDLE>(std::stoull(val, nullptr, 0)), true);
			});
		
		argp.add_argument("targets")
			.help(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_HELP_TARGETS) + 1))
			.remaining();
	}

	void Parse() {
		std::vector<std::string> args;

		if (int nArgs; LPWSTR * szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs)) {
			for (int i = 0; i < nArgs; i++)
				args.emplace_back(Utils::ToUtf8(szArgList[i]));
			LocalFree(szArgList);
		}

		// prevent argparse from taking over --help
		if (std::find(args.begin() + 1, args.end(), std::string("-h")) != args.end()
			|| std::find(args.begin() + 1, args.end(), std::string("-help")) != args.end()) {
			m_help = true;
			return;
		}

		argp.parse_args(args);

		m_action = argp.get<XivAlexDll::LoaderAction>("-a");
		m_launcherType = argp.get<LauncherType>("-l");
		m_quiet = argp.get<bool>("-q");
		m_disableAutoRunAs = argp.get<bool>("-d");
		if (const auto waitHandle = argp.get<Utils::Win32::Process>("--wait-process"))
			waitHandle.Wait();

		if (argp.present("targets")) {
			const auto targets = argp.get<std::vector<std::string>>("targets");
			if (argp.get<bool>("--handle-instead-of-pid")) {
				for (const auto& target : targets) {
					m_targetProcessHandles.emplace_back(reinterpret_cast<HANDLE>(std::stoull(target, nullptr, 0)), true);
				}
				
			} else {
				if (!targets.empty()) {
					m_runProgram = Utils::FromUtf8(targets[0]);
					m_runProgramArgs = Utils::FromUtf8(Utils::Win32::ReverseCommandLineToArgv(std::span(targets.begin() + 1, targets.end())));
				}

				for (const auto& target : targets) {
					size_t idx = 0;
					DWORD pid = 0;

					try {
						pid = std::stoi(target, &idx);
					} catch (std::invalid_argument&) {
						// empty
					} catch (std::out_of_range&) {
						// empty
					}
					if (idx != target.length()) {
						auto buf = Utils::FromUtf8(target);
						CharLowerW(&buf[0]);
						m_targetSuffix.emplace(std::move(buf));
					} else {
						m_targetPids.insert(pid);
					}
				}
			}
		}
	}

	[[nodiscard]] std::wstring GetHelpMessage() const {
		return std::format(
			Utils::Win32::FindStringResourceEx(nullptr, IDS_APP_DESCRIPTION) + 1,
			Utils::FromUtf8(argp.help().str())
		);
	}
} g_parameters;

static std::set<DWORD> GetTargetPidList() {
	std::set<DWORD> pids;
	if (!g_parameters.m_targetPids.empty()) {
		auto list = Utils::Win32::GetProcessList();
		std::sort(list.begin(), list.end());
		std::set_intersection(list.begin(), list.end(), g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end(), std::inserter(pids, pids.end()));
		pids.insert(g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end());
	} else if (g_parameters.m_targetSuffix.empty()) {
		g_parameters.m_targetSuffix.emplace(XivAlex::GameExecutable32NameW);
		g_parameters.m_targetSuffix.emplace(XivAlex::GameExecutable64NameW);
	}
	if (!g_parameters.m_targetSuffix.empty()) {
		for (const auto pid : Utils::Win32::GetProcessList()) {
			Utils::Win32::Process hProcess;
			try {
				hProcess = Utils::Win32::Process(PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
			} catch (std::runtime_error&) {
				// some processes only allow PROCESS_QUERY_INFORMATION,
				// while denying PROCESS_QUERY_LIMITED_INFORMATION.
				try {
					hProcess = Utils::Win32::Process(PROCESS_QUERY_INFORMATION, false, pid);
				} catch (std::runtime_error&) {
					continue;
				}
			}
			try {
				auto pathbuf = hProcess.PathOf().wstring();
				auto suffixFound = false;
				CharLowerW(&pathbuf[0]);
				for (const auto& suffix : g_parameters.m_targetSuffix) {
					if ((suffixFound = pathbuf.ends_with(suffix)))
						break;
				}
				if (suffixFound)
					pids.insert(pid);
			} catch (std::runtime_error& e) {
				OutputDebugStringW(std::format(L"Error for PID {}: {}\n", pid, Utils::FromUtf8(e.what())).c_str());
			}
		}
	}
	return pids;
}

auto OpenProcessForInjection(DWORD pid) {
	return Utils::Win32::Process(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid);
}

bool RequiresAdminAccess(const std::set<DWORD>& pids) {
	try {
		for (const auto pid : pids)
			OpenProcessForInjection(pid);
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() == ERROR_ACCESS_DENIED)
			return true;
	}
	return false;
}

void DoPidTask(DWORD pid, const std::filesystem::path& dllDir, const std::filesystem::path& dllPath) {
	auto process = OpenProcessForInjection(pid);

	if (process.IsProcess64Bits() != Utils::Win32::Process::Current().IsProcess64Bits()) {
		Utils::Win32::RunProgram({
			.path = Utils::Win32::Process::Current().PathOf().parent_path() / (process.IsProcess64Bits() ? XivAlex::XivAlexLoader64NameW : XivAlex::XivAlexLoader32NameW),
			.args = std::format(L"{}-a {} {}",
				g_parameters.m_quiet ? L"-q " : L"",
				LoaderActionToString(g_parameters.m_action),
				process.GetId()),
			.wait = true
		});
		return;
	}

	void* rpModule = process.AddressOf(dllPath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false);
	const auto path = process.PathOf();

	auto loaderAction = g_parameters.m_action;
	if (loaderAction == XivAlexDll::LoaderAction::Ask || loaderAction == XivAlexDll::LoaderAction::Auto) {
		if (rpModule) {
			switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1, MsgboxTitle,
				Utils::Win32::FindStringResourceEx(nullptr, IDS_CONFIRM_INJECT_AGAIN) + 1,
				pid, path,
				Utils::Win32::MB_GetString(IDYES - 1),
				Utils::Win32::MB_GetString(IDNO - 1),
				Utils::Win32::MB_GetString(IDCANCEL - 1)
				)) {
				case IDYES:
					loaderAction = XivAlexDll::LoaderAction::Load;
					break;
				case IDNO:
					loaderAction = XivAlexDll::LoaderAction::Unload;
					break;
				case IDCANCEL:
					loaderAction = XivAlexDll::LoaderAction::Count_;
			}
		} else {
			const auto regionAndVersion = XivAlex::ResolveGameReleaseRegion(path);
			const auto gameConfigFilename = std::format(L"game.{}.{}.json",
				std::get<0>(regionAndVersion),
				std::get<1>(regionAndVersion));
			const auto gameConfigPath = dllDir / gameConfigFilename;
		
			switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1, MsgboxTitle,
				Utils::Win32::FindStringResourceEx(nullptr, IDS_CONFIRM_INJECT) + 1,
				pid, path, gameConfigPath)) {
				case IDYES:
					loaderAction = XivAlexDll::LoaderAction::Load;
					break;
				case IDNO:
					loaderAction = XivAlexDll::LoaderAction::Count_;
			}
		}
	}

	if (loaderAction == XivAlexDll::LoaderAction::Count_)
		return;

	if (loaderAction == XivAlexDll::LoaderAction::Unload && !rpModule)
		return;

	const auto injectedModule = Utils::Win32::InjectedModule(std::move(process), dllPath);
	auto unloadRequired = false;
	const auto cleanup = Utils::CallOnDestruction([&injectedModule, &unloadRequired]() {
		if (unloadRequired)
			injectedModule.Call("EnableXivAlexander", 0, "EnableXivAlexander(0)");
		});

	if (loaderAction == XivAlexDll::LoaderAction::Load) {
		unloadRequired = true;
		if (const auto loadResult = injectedModule.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)"); loadResult != 0)
			throw std::runtime_error(std::format(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_LOAD) + 1), loadResult));
		else
			unloadRequired = false;
	} else if (loaderAction == XivAlexDll::LoaderAction::Unload) {
		unloadRequired = true;
	}
}

int RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args = L"") {
	if (Utils::Win32::RunProgram({
		.path = path,
		.args = args,
		.elevateMode = Utils::Win32::RunProgramParams::CancelIfRequired,
	}))
		return 0;
	return Utils::Win32::RunProgram({
		.args = Utils::FromUtf8(Utils::Win32::ReverseCommandLineToArgv({
			"--disable-runas",
			"-a", LoaderActionToString(XivAlexDll::LoaderAction::Launcher),
			"-l", "select",
			path.string()
		})) + (args.empty() ? L"" : L" " + args),
		.elevateMode = Utils::Win32::RunProgramParams::Force,
	}) ? 0 : 1;
}

int SelectAndRunLauncher() {
	if (!g_parameters.m_runProgram.empty()) {
		return RunProgramRetryAfterElevatingSelfAsNecessary(g_parameters.m_runProgram, g_parameters.m_runProgramArgs);
	}

	try {
		auto throw_on_error = [](HRESULT val) {
			if (!SUCCEEDED(val))
				_com_raise_error(val);
		};

		IFileOpenDialogPtr pDialog;
		DWORD dwFlags;
		static const COMDLG_FILTERSPEC fileTypes[] = {
			{L"FFXIV Boot Files (ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe)", L"ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe"},
			{L"Executable Files (*.exe)", L"*.exe"},
		};
		throw_on_error(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
		throw_on_error(pDialog->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes));
		throw_on_error(pDialog->SetFileTypeIndex(0));
		throw_on_error(pDialog->SetDefaultExtension(L"exe"));
		throw_on_error(pDialog->SetTitle(Utils::Win32::FindStringResourceEx(nullptr, IDS_TITLE_SELECT_BOOT) + 1));
		throw_on_error(pDialog->GetOptions(&dwFlags));
		throw_on_error(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
		throw_on_error(pDialog->Show(nullptr));

		std::wstring fileName;
		{
			IShellItemPtr pResult;
			PWSTR pszFileName;
			throw_on_error(pDialog->GetResult(&pResult));
			throw_on_error(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
			fileName = pszFileName;
			CoTaskMemFree(pszFileName);
		}
		
		return RunProgramRetryAfterElevatingSelfAsNecessary(fileName);
	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(nullptr, MB_ICONERROR, MsgboxTitle,  Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_UNEXPECTED), e.what() ? Utils::FromUtf8(e.what()) : L"Unknown");
	} catch (_com_error& e) {
		if (e.Error() == HRESULT_FROM_WIN32(ERROR_CANCELLED))
			return 1;
		const auto err = static_cast<const wchar_t*>(e.Description());
		throw std::runtime_error(err ? Utils::ToUtf8(err) : "Unknown");
	}
	return 0;
}

int RunLauncher() {
	try {
		XivAlexDll::EnableInjectOnCreateProcess(XivAlexDll::InjectOnCreateProcessAppFlags::Use | XivAlexDll::InjectOnCreateProcessAppFlags::InjectAll);
		
		const auto launchers = XivAlex::FindGameLaunchers();
		switch (g_parameters.m_launcherType) {
			case LauncherType::Auto:
			{
				if (launchers.empty() || !g_parameters.m_runProgram.empty())
					return SelectAndRunLauncher();
				else if (launchers.size() == 1)
					return RunProgramRetryAfterElevatingSelfAsNecessary(launchers.begin()->second.BootApp);
				else {
					for (const auto& it : launchers) {
						if (Utils::Win32::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION, MsgboxTitle,
							Utils::Win32::FindStringResourceEx(nullptr, IDS_CONFIRM_LAUNCH) + 1,
							argparse::details::repr(it.first), it.second.RootPath) == IDYES)
							RunProgramRetryAfterElevatingSelfAsNecessary(it.second.BootApp, L"");
					}
				}
				return 0;
			}

			case LauncherType::Select:
				return SelectAndRunLauncher();

			case LauncherType::International:
				return RunProgramRetryAfterElevatingSelfAsNecessary(launchers.at(XivAlex::GameRegion::International).BootApp);
			
			case LauncherType::Korean:
				return RunProgramRetryAfterElevatingSelfAsNecessary(launchers.at(XivAlex::GameRegion::Korean).BootApp);
			
			case LauncherType::Chinese:
				return RunProgramRetryAfterElevatingSelfAsNecessary(launchers.at(XivAlex::GameRegion::Chinese).BootApp);
		}
	} catch (std::out_of_range&) {
		if (g_parameters.m_action == XivAlexDll::LoaderAction::Auto) {
			if (!g_parameters.m_quiet)
				SelectAndRunLauncher();

		} else if (!g_parameters.m_quiet) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONINFORMATION, MsgboxTitle, Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_NOT_FOUND) + 1);
		}
		return -1;

	} catch (std::exception& e) {
		if (!g_parameters.m_quiet)
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_UNEXPECTED) + 1, e.what());
		return -1;
	}
	return 0;
}

static bool RequiresElevationForUpdate(std::vector<DWORD> excludedPid) {
	// check if other ffxiv processes else than excludedPid exists, and if they're inaccessible.
	for (const auto pid : GetTargetPidList()) {
		try {
			if (std::find(excludedPid.begin(), excludedPid.end(), pid) == excludedPid.end())
				OpenProcessForInjection(pid);
		} catch (...) {
			return true;
		}
	}
	return false;
}

#pragma optimize("", off)
template<typename...Args>
Utils::CallOnDestruction ShowLazyProgress(bool explicitCancel, const UINT nFormatResId, Args...args) {
	const auto format = Utils::Win32::FindStringResourceEx(nullptr, nFormatResId) + 1;
	auto cont = std::make_shared<bool>(true);
	auto t = Utils::Win32::Thread(L"UpdateStatus", [explicitCancel, msg = std::format(format, std::forward<Args>(args)...), cont]() {
		while (*cont) {
			if (Utils::Win32::MessageBoxF(nullptr, (explicitCancel ? MB_OKCANCEL : MB_OK) | MB_ICONINFORMATION, MsgboxTitle, msg) == (explicitCancel ? IDCANCEL : IDOK)) {
				if (*cont)
					Utils::Win32::Process::Current().Terminate(0);
				else
					return;
			}
		}
	});
	return { [t = std::move(t), cont = std::move(cont)]() {
		*cont = false;
		const auto tid = t.GetId();
		while (t.Wait(100) == WAIT_TIMEOUT) {
			HWND hwnd = nullptr;
			while ( (hwnd = FindWindowExW(nullptr, hwnd, nullptr, nullptr))) {
				const auto hwndThreadId = GetWindowThreadProcessId(hwnd, nullptr);
				if (tid == hwndThreadId) {
					SendMessageW(hwnd, WM_CLOSE, 0, 0);
				}
			}
		}
	} };
}
#pragma optimize("", on)

static void PerformUpdateAndExitIfSuccessful(std::vector<Utils::Win32::Process> gameProcesses, const std::string& url, const std::filesystem::path& updateZip) {
	std::vector<DWORD> prevProcessIds;
	std::transform(gameProcesses.begin(), gameProcesses.end(), std::back_inserter(prevProcessIds), [](const Utils::Win32::Process& k) { return k.GetId(); });
	const auto& currentProcess = Utils::Win32::Process::Current();
	const auto launcherDir = currentProcess.PathOf().parent_path();
	const auto tempExtractionDir = launcherDir / L"__UPDATE__";
	const auto launcherPath32 = launcherDir / XivAlex::XivAlexLoader32NameW;
	const auto launcherPath64 = launcherDir / XivAlex::XivAlexLoader64NameW;

	{
		const auto progress = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_DOWNLOADING_FILES);

		try {
			for (int i = 0; i < 2; ++i) {
				try {
					if (g_parameters.m_debugUpdate) {
						const auto tempPath = launcherDir / L"updatesourcetest.zip";
						libzippp::ZipArchive arc(tempPath.string());
						arc.open(libzippp::ZipArchive::Write);
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexDll32NameW), Utils::ToUtf8(launcherDir / XivAlex::XivAlexDll32NameW));
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexDll64NameW), Utils::ToUtf8(launcherDir / XivAlex::XivAlexDll64NameW));
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexLoader32NameW), Utils::ToUtf8(launcherDir / XivAlex::XivAlexLoader32NameW));
						arc.addFile(Utils::ToUtf8(XivAlex::XivAlexLoader64NameW), Utils::ToUtf8(launcherDir / XivAlex::XivAlexLoader64NameW));
						arc.close();
						copy(tempPath, updateZip);
					} else {
						if (!exists(updateZip) || i == 1) {
							std::ofstream f(updateZip, std::ios::binary);
							curlpp::Easy req;
							req.setOpt(curlpp::options::Url(url));
							req.setOpt(curlpp::options::UserAgent("Mozilla/5.0"));
							req.setOpt(curlpp::options::FollowLocation(true));
							f << req;
						}
					}
					
					remove_all(tempExtractionDir);

					const auto hFile = Utils::Win32::Handle(CreateFileW(updateZip.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr),
						INVALID_HANDLE_VALUE, "CreateFileW({}, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)", updateZip);
					LARGE_INTEGER fileSize;
					if (!GetFileSizeEx(hFile, &fileSize))
						throw Utils::Win32::Error("GetFileSizeEx");
					if (fileSize.QuadPart > 128 * 1024 * 1024)
						throw Utils::Win32::Error(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_UPDATE_FILE_TOO_BIG) + 1));
					const auto hMapFile = Utils::Win32::Handle(CreateFileMappingW(hFile, nullptr, PAGE_READONLY, fileSize.HighPart, fileSize.LowPart, nullptr), nullptr, "CreateFileMappingW");
					const auto pMapped = Utils::Win32::Closeable<void*, UnmapViewOfFile>(MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, fileSize.LowPart));
					
					const auto pArc = libzippp::ZipArchive::fromBuffer(pMapped, fileSize.LowPart);
					const auto freeArc = Utils::CallOnDestruction([pArc](){ pArc->close(); delete pArc; });
					for (const auto& entry : pArc->getEntries()) {
						const auto pData = static_cast<char*>(entry.readAsBinary());
						const auto freeData = Utils::CallOnDestruction([pData]() { delete[] pData; });
						const auto targetPath = launcherDir / tempExtractionDir / entry.getName();
						create_directories(targetPath.parent_path());
						std::ofstream f(targetPath, std::ios::binary);
						f.write(pData, entry.getSize());
					}
					break;
				} catch (...) {
					if (url.empty())
						return;
					
					if (i == 1)
						throw;
				}
			}
		} catch (const std::exception&) {
			remove_all(tempExtractionDir);
			remove(updateZip);
			throw;
		}
	}
	// TODO: ask user to quit all FFXIV related processes, except the game itself.
	
	if (RequiresElevationForUpdate(prevProcessIds)) {
		Utils::Win32::RunProgram({
			.args = std::format(L"--action {}", LoaderActionToString(XivAlexDll::LoaderAction::UpdateCheck)),
			.elevateMode = Utils::Win32::RunProgramParams::Force,
		});
		return;
	}
	
	{
		const auto progress = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_PREPARING_FILES);

		std::vector unloadTargets = gameProcesses;
		for (const auto pid : Utils::Win32::GetProcessList()) {
			if (pid == GetCurrentProcessId() || std::find(prevProcessIds.begin(), prevProcessIds.end(), pid) != prevProcessIds.end())
				continue;

			std::filesystem::path processPath;
			
			try {
				processPath = Utils::Win32::Process(PROCESS_QUERY_LIMITED_INFORMATION, false, pid).PathOf();
			} catch (const std::exception&) {
				try {
					processPath = Utils::Win32::Process(PROCESS_QUERY_INFORMATION, false, pid).PathOf();
				} catch (const std::exception&) {
					// ¯\_(ツ)_/¯
					// unlikely that we can't access information of process that has anything to do with xiv,
					// unless it's antivirus, in which case we can't do anything, which will result in failure anyway.
					continue;
				}
			}
			if (processPath == launcherPath32 || processPath == launcherPath64) {
				while (true) {
					try {
						const auto hProcess = Utils::Win32::Process(PROCESS_TERMINATE | SYNCHRONIZE, false, pid);
						if (hProcess.Wait(0) != WAIT_TIMEOUT)
							break;
						hProcess.Terminate(0);
					} catch (const Utils::Win32::Error& e) {
						if (e.Code() == ERROR_INVALID_PARAMETER)  // this process already gone
							break;
						if (Utils::Win32::MessageBoxF(nullptr, MB_OKCANCEL, MsgboxTitle, Utils::Win32::FindStringResourceEx(nullptr, IDS_UPDATE_PROCESS_KILL_FAILURE) + 1, pid, e.what()) == IDCANCEL)
							return;
					}
				}
				
			} else if (processPath.filename() == XivAlex::GameExecutable64NameW || processPath.filename() == XivAlex::GameExecutable32NameW) {
				auto process = OpenProcessForInjection(pid);
				gameProcesses.emplace_back(process);
				unloadTargets.emplace_back(std::move(process));
			} else {
				try {
					auto process = OpenProcessForInjection(pid);
					const auto is64 = process.IsProcess64Bits();
					const auto modulePath = launcherDir / (is64 ? XivAlex::XivAlexDll64NameW : XivAlex::XivAlexDll32NameW);
					if (process.AddressOf(modulePath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false))
						unloadTargets.emplace_back(std::move(process));
				} catch (...) {
				}
			}
		}
		
		LaunchXivAlexLoaderWithTargetHandles(unloadTargets, XivAlexDll::LoaderAction::Internal_Cleanup_Handle, true);
		LaunchXivAlexLoaderWithTargetHandles(gameProcesses, XivAlexDll::LoaderAction::Internal_Update_Step2_ReplaceFiles, false, (tempExtractionDir / XivAlex::XivAlexLoader64NameW).c_str(), currentProcess);
		
		currentProcess.Terminate(0);
	}
}

static void CheckForUpdates(std::vector<Utils::Win32::Process> prevProcesses) {
	const auto updateZip = Utils::Win32::Process::Current().PathOf().parent_path() / "update.zip";
	if (exists(updateZip))
		PerformUpdateAndExitIfSuccessful(prevProcesses, "", updateZip);
	
	try {
		std::vector<int> remote, local;
		XivAlex::VersionInformation up;
		if (g_parameters.m_debugUpdate) {
			local = {0, 0, 0, 0};
			remote = {0, 0, 0, 1};
		} else {
			const auto checking = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_CHECKING);
			const auto [selfFileVersion, selfProductVersion] = Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr));
			up = XivAlex::CheckUpdates();		
			const auto remoteS = Utils::StringSplit(up.Name.substr(1), ".");
			const auto localS = Utils::StringSplit(selfProductVersion, ".");
			for (const auto& s : remoteS)
				remote.emplace_back(std::stoi(s));
			for (const auto& s : localS)
				local.emplace_back(std::stoi(s));
		}
		if (local.size() != 4 || remote.size() != 4)
			throw std::runtime_error(Utils::ToUtf8(Utils::Win32::FindStringResourceEx(nullptr, IDS_UPDATE_FILE_TOO_BIG) + 1));
		if (local >= remote) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONINFORMATION, MsgboxTitle, 
				Utils::Win32::FindStringResourceEx(nullptr, IDS_UPDATE_UNAVAILABLE) + 1,
				local[0], local[1], local[2], local[3], remote[0], remote[1], remote[2], remote[3], up.PublishDate);
			return;
		}
		
		while (true) {
			switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNOCANCEL, MsgboxTitle, std::format(
				Utils::Win32::FindStringResourceEx(nullptr, IDS_UPDATE_CONFIRM) + 1,
				remote[0], remote[1], remote[2], remote[3], up.PublishDate, local[0], local[1], local[2], local[3],
				Utils::Win32::MB_GetString(IDYES - 1),
				Utils::Win32::MB_GetString(IDNO - 1),
				Utils::Win32::MB_GetString(IDCANCEL - 1)
			).c_str())) {
				case IDYES:
					ShellExecuteW(nullptr, L"open", Utils::Win32::FindStringResourceEx(nullptr, IDS_URL_RELEASES) + 1, nullptr, nullptr, SW_SHOW);
					break;
				
				case IDNO:
					PerformUpdateAndExitIfSuccessful(std::move(prevProcesses), up.DownloadLink, updateZip);
					return;

				case IDCANCEL:
					return;
			}	
		}
	} catch (const std::exception& e) {
		Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_UNEXPECTED) + 1, e.what());
	}
}

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	if (!SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
		std::abort();

	try {
		g_parameters.Parse();
	} catch (std::exception& err) {
		Utils::Win32::MessageBoxF(nullptr, MB_ICONWARNING, MsgboxTitle, Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_COMMAND_LINE) + 1, err.what(), g_parameters.GetHelpMessage());
		return -1;
	}
	if (g_parameters.m_help) {
		Utils::Win32::MessageBoxF(nullptr, MB_OK, MsgboxTitle, g_parameters.GetHelpMessage().c_str());
		return 0;
	}
	if (g_parameters.m_action == XivAlexDll::LoaderAction::Web) {
		ShellExecuteW(nullptr, L"open", Utils::Win32::FindStringResourceEx(nullptr, IDS_URL_MAIN) + 1, nullptr, nullptr, SW_SHOW);
		return 0;
	}

	std::string debugPrivilegeError = "OK.";
	try {
		Utils::Win32::AddDebugPrivilege();
	} catch (const std::exception& err) {
		debugPrivilegeError = Utils::ToUtf8(std::format(Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_SEDEBUGPRIVILEGE) + 1, err.what()));
	}

	const auto& currentProcess = Utils::Win32::Process::Current();
	const auto dllDir = currentProcess.PathOf().parent_path();
	const auto dllPath = dllDir / XivAlex::XivAlexDllNameW;

	try {
		CheckPackageVersions();
	} catch (std::exception& e) {
		if (Utils::Win32::MessageBoxF(nullptr, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1, MsgboxTitle,
			Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_COMPONENTS) + 1,
			Utils::FromUtf8(e.what())
		) == IDYES) {
			ShellExecuteW(nullptr, L"open", Utils::Win32::FindStringResourceEx(nullptr, IDS_URL_RELEASES) + 1, nullptr, nullptr, SW_SHOW);
		}
		return -1;
	}
	
	try {
#ifdef _DEBUG
		Utils::Win32::MessageBoxF(nullptr, MB_OK, MsgboxTitle, L"Action: {}", argparse::details::repr(g_parameters.m_action));
#endif
		if (g_parameters.m_action == XivAlexDll::LoaderAction::Internal_Inject_HookEntryPoint) {
			for (const auto& x : g_parameters.m_targetProcessHandles)
				XivAlexDll::PatchEntryPointForInjection(x);
			return 0;
			
		} else if (g_parameters.m_action == XivAlexDll::LoaderAction::Internal_Inject_LoadXivAlexanderImmediately || g_parameters.m_action == XivAlexDll::LoaderAction::Internal_Cleanup_Handle) {
			std::vector<Utils::Win32::Process> mine, defer;
			for (auto& process : g_parameters.m_targetProcessHandles) {
				if (process.IsProcess64Bits() == (INT64_MAX == INTPTR_MAX))
					mine.emplace_back(std::move(process));
				else
					defer.emplace_back(std::move(process));
			}
			for (auto& process : mine) {
				try {
					const auto m = Utils::Win32::InjectedModule(std::move(process), dllPath);
					if (g_parameters.m_action == XivAlexDll::LoaderAction::Internal_Inject_LoadXivAlexanderImmediately)
						m.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)");
					else
						m.Call("DisableAllApps", nullptr, "DisableAllApps(0)");
				} catch (...) {
					// pass
				}
			}

			if (!defer.empty()) {
				LaunchXivAlexLoaderWithTargetHandles(defer, g_parameters.m_action, true, (dllDir / XivAlex::XivAlexLoaderOppositeNameW).c_str());
			}
			return 0;
			
		} else if (g_parameters.m_action == XivAlexDll::LoaderAction::UpdateCheck) {
			if (!Utils::Win32::Process::Current().IsProcess64Bits()) {
				SYSTEM_INFO si;
				GetNativeSystemInfo(&si);
				if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
					std::vector<HANDLE> handles;
					LaunchXivAlexLoaderWithTargetHandles(g_parameters.m_targetProcessHandles, XivAlexDll::LoaderAction::UpdateCheck, false, (dllDir / XivAlex::XivAlexLoader64NameW).c_str());
					return 0;
				}
			}
			
			CheckForUpdates(std::move(g_parameters.m_targetProcessHandles));
			return 0;
			
		} else if (g_parameters.m_action == XivAlexDll::LoaderAction::Internal_Update_Step2_ReplaceFiles) {
			const auto checking = ShowLazyProgress(true, IDS_UPDATE_PROGRESS_UPDATING_FILES);
			const auto temporaryUpdatePath = Utils::Win32::Process::Current().PathOf().parent_path();
			const auto targetUpdatePath = temporaryUpdatePath.parent_path();
			if (temporaryUpdatePath.filename() != L"__UPDATE__")
				throw std::runtime_error("cannot update outside of update process");
			copy(
				temporaryUpdatePath,
				targetUpdatePath,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing
			);
			LaunchXivAlexLoaderWithTargetHandles(g_parameters.m_targetProcessHandles, XivAlexDll::LoaderAction::Internal_Update_Step3_CleanupFiles, false, (targetUpdatePath / XivAlex::XivAlexLoader64NameW).c_str(), currentProcess);
			return 0;
			
		} else if (g_parameters.m_action == XivAlexDll::LoaderAction::Internal_Update_Step3_CleanupFiles) {
			std::vector<Utils::Win32::Process> processes;
			{
				const auto checking = ShowLazyProgress(true, IDS_UPDATE_CLEANUP_UPDATE);
				remove_all(Utils::Win32::Process::Current().PathOf().parent_path() / L"__UPDATE__");
				remove(Utils::Win32::Process::Current().PathOf().parent_path() / L"update.zip");
			}
			{
				const auto checking = ShowLazyProgress(true, IDS_UPDATE_RELOADING_XIVALEXANDER);
				auto pids = GetTargetPidList();
				for (const auto& process : processes)
					pids.erase(process.GetId());
				
				if (!g_parameters.m_targetProcessHandles.empty())
					LaunchXivAlexLoaderWithTargetHandles(g_parameters.m_targetProcessHandles, XivAlexDll::LoaderAction::Internal_Inject_LoadXivAlexanderImmediately, true);
				
				if (!pids.empty()) {
					for (const auto& pid : pids) {
						g_parameters.m_action = XivAlexDll::LoaderAction::Load;
						g_parameters.m_quiet = true;
						try {
							DoPidTask(pid, dllDir, dllPath);
						} catch (...) {
						}
					}
				}
			}
			{
				const auto checking = ShowLazyProgress(false, IDS_UPDATE_COMPLETE);
				Sleep(3000);
			}
			return 0;
		}
	} catch (const std::exception& e) {
		Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle,
			Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_UNEXPECTED) + 1, e.what());
		return -1;
	}

	if (g_parameters.m_action == XivAlexDll::LoaderAction::Launcher)
		return RunLauncher();

	const auto pids = GetTargetPidList();

	if (pids.empty()) {
		if (g_parameters.m_action == XivAlexDll::LoaderAction::Auto) {
			return RunLauncher();
		}
		if (!g_parameters.m_quiet) {
			std::wstring errors;
			if (g_parameters.m_targetPids.empty() && g_parameters.m_targetSuffix.empty())
				errors = std::format(Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_NO_FFXIV_PROCESS) + 1, XivAlex::GameExecutableNameW);
			else
				errors = Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_NO_MATCHING_PROCESS) + 1;
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, errors);
		}
		return -1;
	}

	if (!g_parameters.m_disableAutoRunAs && !Utils::Win32::IsUserAnAdmin() && RequiresAdminAccess(pids)) {
		try {
			return Utils::Win32::RunProgram({
				.args = std::format(L"--disable-runas {}", lpCmdLine),
				.wait = true,
				.elevateMode = Utils::Win32::RunProgramParams::Force,
			});
		} catch (std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, Utils::Win32::FindStringResourceEx(nullptr, IDS_ERROR_RESTART_ADMIN) + 1, e.what());
		}
		return 0;
	}

	for (const auto pid : pids) {
		try {
			DoPidTask(pid, dllDir, dllPath);
		} catch (std::exception& e) {
			if (!g_parameters.m_quiet)
				Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle,
					L"PID: {}\n"
					L"\n"
					L"SeDebugPrivilege: {}\n"
					L"\n"
					L"* {}",
					pid,
					debugPrivilegeError,
					e.what()
				);
		}
	}
	return 0;
}
