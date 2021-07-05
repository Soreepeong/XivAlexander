#include "pch.h"

const auto MsgboxTitle = L"XivAlexander Loader";

static
void CheckPackageVersions() {
	const auto dir = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto modules = std::vector{
		Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexLoader32NameW),
		Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexLoader64NameW),
		Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexDll32NameW),
		Utils::Win32::FormatModuleVersionString(dir / XivAlex::XivAlexDll64NameW),
	};
	for (size_t i = 1; i < modules.size(); ++i) {
		if (modules[0].first != modules[i].first || modules[0].second != modules[i].second)
			throw std::runtime_error("Inconsistent files.");
	}
}

enum class LoaderAction : int {
	Auto,
	Ask,
	Load,
	Unload,
	Launcher,
	Count_,  // for internal use only
};

template <>
std::string argparse::details::repr(LoaderAction const& val) {
	switch (val) {
		case LoaderAction::Ask: return "ask";
		case LoaderAction::Load: return "load";
		case LoaderAction::Unload: return "unload";
		case LoaderAction::Launcher: return "launcher";
		case LoaderAction::Auto: return "auto";
	}
	return std::format("({})", static_cast<int>(val));
}

LoaderAction ParseLoaderAction(std::string val) {
	auto valw = Utils::FromUtf8(val);
	CharLowerW(&valw[0]);
	val = Utils::ToUtf8(valw);
	for (size_t i = 0; i < static_cast<size_t>(LoaderAction::Count_); ++i) {
		const auto compare = argparse::details::repr(static_cast<LoaderAction>(i));
		auto equal = true;
		for (size_t j = 0; equal && j < val.length() && j < compare.length(); ++j) {
			equal = val[j] == compare[j];
		}
		if (equal)
			return static_cast<LoaderAction>(i);
	}
	throw std::runtime_error("Invalid action");
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
		case XivAlex::GameRegion::International: return "International";
		case XivAlex::GameRegion::Korean: return "Korean";
		case XivAlex::GameRegion::Chinese: return "Chinese";
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

	throw std::runtime_error("Invalid launcher type");
}

class XivAlexanderLoaderParameter {
public:
	argparse::ArgumentParser argp;

	LoaderAction m_action = LoaderAction::Auto;
	LauncherType m_launcherType = LauncherType::Auto;
	bool m_quiet = false;
	bool m_help = false;
	bool m_web = false;
	bool m_disableAutoRunAs = true;
	bool m_injectIntoStdinHandle = false;
	std::set<DWORD> m_targetPids{};
	std::set<std::wstring> m_targetSuffix{};
	std::wstring m_runProgram;
	std::wstring m_runProgramArgs;

	XivAlexanderLoaderParameter()
		: argp("XivAlexanderLoader") {

		argp.add_argument("-a", "--action")
			.help("specify action (possible values: auto, ask, load, unload, launcher)")
			.required()
			.nargs(1)
			.default_value(LoaderAction::Auto)
			.action([](const std::string& val) { return ParseLoaderAction(val); });
		argp.add_argument("-l", "--launcher")
			.help("specify launcher (possible values: auto, select, international, korean, chinese)")
			.required()
			.nargs(1)
			.default_value(LauncherType::Auto)
			.action([](const std::string& val) { return ParseLauncherType(val); });
		argp.add_argument("-q", "--quiet")
			.help("disable error messages")
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("-d", "--disable-runas")
			.help("do not try to run as administrator in any case")
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("--web")
			.help("open github repository at https://github.com/Soreepeong/XivAlexander and exit")
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("--inject-into-stdin-handle")
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("targets")
			.help("List of target process ID or path suffix if injecting. Path to program to execute if launching.")
			.default_value(std::vector<std::string>())
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

		m_action = argp.get<LoaderAction>("-a");
		m_launcherType = argp.get<LauncherType>("-l");
		m_quiet = argp.get<bool>("-q");
		m_web = argp.get<bool>("--web");
		m_disableAutoRunAs = argp.get<bool>("-d");
		m_injectIntoStdinHandle = argp.get<bool>("--inject-into-stdin-handle");

		const auto targets = argp.get<std::vector<std::string>>("targets");
		if (!targets.empty()) {
			m_runProgram = Utils::FromUtf8(targets[0]);
			m_runProgramArgs = Utils::Win32::ReverseCommandLineToArgvW(std::span(targets.begin() + 1, targets.end()));
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
			} else
				m_targetPids.insert(pid);
		}
	}

	[[nodiscard]] std::wstring GetHelpMessage() const {
		return std::format(L"XivAlexanderLoader: loads XivAlexander into game process (DirectX 11 version, x64 only).\n\n{}",
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
				argparse::details::repr(g_parameters.m_action),
				process.GetId()),
			.wait = true
			});
		return;
	}

	void* rpModule = process.AddressOf(dllPath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false);
	const auto path = process.PathOf();

	auto loaderAction = g_parameters.m_action;
	if (loaderAction == LoaderAction::Ask || loaderAction == LoaderAction::Auto) {
		if (rpModule) {
			switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1, MsgboxTitle,
				L"XivAlexander detected in FFXIV Process ({}:{})\n"
				L"Press Yes to try loading again if it hasn't loaded properly,\n"
				L"Press No to unload, or\n"
				L"Press Cancel to skip.\n"
				L"\n"
				L"Note: your anti-virus software will probably classify DLL injection as a malicious action, "
				L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.",
				pid, path)) {
				case IDYES:
					loaderAction = LoaderAction::Load;
					break;
				case IDNO:
					loaderAction = LoaderAction::Unload;
					break;
				case IDCANCEL:
					loaderAction = LoaderAction::Count_;
			}
		} else {
			const auto regionAndVersion = XivAlex::ResolveGameReleaseRegion(path);
			const auto gameConfigFilename = std::format(L"game.{}.{}.json",
				std::get<0>(regionAndVersion),
				std::get<1>(regionAndVersion));
			const auto gameConfigPath = dllDir / gameConfigFilename;

			if (!g_parameters.m_quiet && !exists(gameConfigPath)) {
				switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1, MsgboxTitle,
					L"FFXIV Process found:\n"
					L"* PID: {}\n"
					L"* Path: {}\n"
					L"* Game Version Configuration File: {}\n"
					L"Continue loading XivAlexander into this process?\n"
					L"\n"
					L"Notes\n"
					L"* Corresponding game version configuration file for this process does not exist. "
					L"You may want to check your game installation path, and edit the right entry in the above file first.\n"
					L"* Your anti-virus software will probably classify DLL injection as a malicious action, "
					L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.",
					pid, path, gameConfigPath)) {
					case IDYES:
						loaderAction = LoaderAction::Load;
						break;
					case IDNO:
						loaderAction = LoaderAction::Count_;
				}
			} else {
				switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1, MsgboxTitle,
					L"FFXIV Process found:\n"
					L"* Process ID: {}\n"
					L"* Path: {}\n"
					L"* Game Version Configuration File: {}\n"
					L"Continue loading XivAlexander into this process?\n"
					L"\n"
					L"Note: your anti-virus software will probably classify DLL injection as a malicious action, "
					L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.",
					pid, path, gameConfigPath)) {
					case IDYES:
						loaderAction = LoaderAction::Load;
						break;
					case IDNO:
						loaderAction = LoaderAction::Count_;
				}
			}
		}
	}

	if (loaderAction == LoaderAction::Count_)
		return;

	if (loaderAction == LoaderAction::Unload && !rpModule)
		return;

	const auto injectedModule = Utils::Win32::InjectedModule(std::move(process), dllPath);
	auto unloadRequired = false;
	const auto cleanup = Utils::CallOnDestruction([&injectedModule, &unloadRequired]() {
		if (unloadRequired)
			injectedModule.Call("EnableXivAlexander", 0, "EnableXivAlexander(0)");
		// injectedModule.Call("CallFreeLibrary", injectedModule.Address(), "CallFreeLibrary");
		});

	if (loaderAction == LoaderAction::Load) {
		unloadRequired = true;
		if (const auto loadResult = injectedModule.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)"); loadResult != 0)
			throw std::runtime_error(std::format("Failed to start the addon: exit code {}", loadResult));
		else
			unloadRequired = false;
	} else if (loaderAction == LoaderAction::Unload) {
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
		.args = Utils::Win32::ReverseCommandLineToArgvW({"--disable-runas", "-a", "launcher", "-l", "select", path.string()}) + (args.empty() ? L"" : L" " + args),
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
		throw_on_error(pDialog->SetTitle(L"Select FFXIV Boot program"));
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
				throw std::runtime_error("The selected file does not have a filesystem path.");
			fileName = pszFileName;
			CoTaskMemFree(pszFileName);
		}
		
		return RunProgramRetryAfterElevatingSelfAsNecessary(fileName);
	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(nullptr, MB_ICONERROR, MsgboxTitle, L"Unable to continue: {}", e.what() ? Utils::FromUtf8(e.what()) : L"Unknown");
	} catch (_com_error& e) {
		if (e.Error() == HRESULT_FROM_WIN32(ERROR_CANCELLED))
			return 1;
		const auto err = static_cast<const wchar_t*>(e.Description());
		throw std::runtime_error(err ? Utils::ToUtf8(err) : "unknown error");
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
							L"Launch {} version of the game?\n\nInstallation path: {}",
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
		if (g_parameters.m_action == LoaderAction::Auto) {
			if (!g_parameters.m_quiet)
				SelectAndRunLauncher();

		} else if (!g_parameters.m_quiet) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONINFORMATION, MsgboxTitle, L"No FFXIV installation cold be detected. Please specify launcher path.");
		}
		return -1;

	} catch (std::exception& e) {
		if (!g_parameters.m_quiet)
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, L"Error occurred: {}", e.what());
		return -1;
	}
	return 0;
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
		Utils::Win32::MessageBoxF(nullptr, MB_ICONWARNING, MsgboxTitle, L"Faild to parse command line arguments.\n\n{}\n\n{}", err.what(), g_parameters.GetHelpMessage());
		return -1;
	}
	if (g_parameters.m_help) {
		Utils::Win32::MessageBoxF(nullptr, MB_OK, MsgboxTitle, g_parameters.GetHelpMessage().c_str());
		return 0;
	}
	if (g_parameters.m_web) {
		ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander", nullptr, nullptr, SW_SHOW);
		return 0;
	}

	const auto dllDir = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto dllPath = dllDir / XivAlex::XivAlexDllNameW;

	try {
		CheckPackageVersions();
	} catch (std::exception& e) {
		if (Utils::Win32::MessageBoxF(nullptr, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1, MsgboxTitle,
			L"Failed to verify XivAlexander.dll and XivAlexanderLoader.exe have the matching versions ({}).\n\nDo you want to download again from Github?",
			Utils::FromUtf8(e.what())
		) == IDYES) {
			ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander/releases", nullptr, nullptr, SW_SHOW);
		}
		return -1;
	}

	if (g_parameters.m_injectIntoStdinHandle) {
		DWORD read;
		uint64_t val;
		try {
			if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &val, sizeof val, &read, nullptr) || read != sizeof val)
				throw Utils::Win32::Error("ReadFile");

			XivAlexDll::PatchEntryPointForInjection(Utils::Win32::Process().Attach(reinterpret_cast<HANDLE>(static_cast<size_t>(val)), true, "null handle is invalid"));
			return 0;
		} catch (const std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, L"Error occurred: {}", e.what());
			return -1;
		}
	}

	if (g_parameters.m_action == LoaderAction::Launcher)
		return RunLauncher();

	std::string debugPrivilegeError = "OK.";
	try {
		Utils::Win32::AddDebugPrivilege();
	} catch (const std::exception& err) {
		debugPrivilegeError = std::format("Failed to obtain.\n* Try running this program as Administrator.\n* {}", err.what());
	}

	const auto pids = GetTargetPidList();

	if (pids.empty()) {
		if (g_parameters.m_action == LoaderAction::Auto) {
			return RunLauncher();
		}
		if (!g_parameters.m_quiet) {
			std::wstring errors;
			if (g_parameters.m_targetPids.empty() && g_parameters.m_targetSuffix.empty())
				errors = std::format(L"{} not found. Run the game first, and then try again.", XivAlex::GameExecutableNameW);
			else
				errors = L"No matching process found.";
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
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, L"Failed to restart as admin: {}", e.what());
		}
		return 0;
	}

	for (const auto pid : pids) {
		try {
			DoPidTask(pid, dllDir, dllPath);
		} catch (std::exception& e) {
			if (!g_parameters.m_quiet)
				Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle,
					L"Process ID: {}\n"
					L"\n"
					L"Debug Privilege: {}\n"
					L"\n"
					L"Error:\n"
					L"* {}",
					pid,
					debugPrivilegeError,
					e.what()
				);
		}
	}
	return 0;
}
