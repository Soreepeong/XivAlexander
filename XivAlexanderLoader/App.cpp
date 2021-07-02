#include "pch.h"

const auto MsgboxTitle = L"XivAlexander Loader";

extern "C" __declspec(dllimport) int __stdcall PatchEntryPointForInjection(HANDLE hProcess);

static
void CheckDllVersion(HMODULE hModule) {
	auto [dllFileVersion, dllProductVersion] = Utils::Win32::FormatModuleVersionString(hModule);
	auto [selfFileVersion, selfProductVersion] = Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr));

	if (dllFileVersion != selfFileVersion)
		throw std::runtime_error(std::format("File versions do not match. (XivAlexanderLoader.exe: {}, XivAlexander.dll: {})",
			selfFileVersion, dllFileVersion));

	if (dllProductVersion != selfProductVersion)
		throw std::runtime_error(std::format("Product versions do not match. (XivAlexanderLoader.exe: {}, XivAlexander.dll: {})",
			selfProductVersion, selfFileVersion));
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
	International,
	Korean,
	Chinese,
	Count_,  // for internal use only
};

template <>
std::string argparse::details::repr(LauncherType const& val) {
	switch (val) {
		case LauncherType::Auto: return "auto";
		case LauncherType::International: return "international";
		case LauncherType::Korean: return "korean";
		case LauncherType::Chinese: return "chinese";
	}
	return std::format("({})", static_cast<int>(val));
}

LauncherType ParseLauncherType(std::string val) {
	auto valw = Utils::FromUtf8(val);
	CharLowerW(&valw[0]);
	val = Utils::ToUtf8(valw);
	for (size_t i = 0; i < static_cast<size_t>(LauncherType::Count_); ++i) {
		const auto compare = argparse::details::repr(static_cast<LauncherType>(i));
		auto equal = true;
		for (size_t j = 0; equal && j < val.length() && j < compare.length(); ++j) {
			equal = val[j] == compare[j];
		}
		if (equal)
			return static_cast<LauncherType>(i);
	}
	if (val[0] == 'e' || val[0] == 'd' || val[0] == 'g' || val[0] == 'f' || val[0] == 'j')
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

	XivAlexanderLoaderParameter()
		: argp("XivAlexanderLoader") {

		argp.add_argument("-a", "--action")
			.help("specify action (possible values: auto, ask, load, unload, launcher)")
			.required()
			.nargs(1)
			.default_value(LoaderAction::Auto)
			.action([](const std::string& val) { return ParseLoaderAction(val); });
		argp.add_argument("-l", "--launcher")
			.help("specify launcher (possible values: auto, international, korean, chinese)")
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

	void Parse(LPWSTR lpCmdLine) {
		std::vector<std::string> args;

		args.push_back(Utils::ToUtf8(Utils::Win32::Process::Current().PathOf()));

		if (wcslen(lpCmdLine) > 0) {
			int nArgs;
			LPWSTR* szArgList = CommandLineToArgvW(lpCmdLine, &nArgs);
			for (int i = 0; i < nArgs; i++)
				args.push_back(Utils::ToUtf8(szArgList[i]));
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

		for (const auto& target : argp.get<std::vector<std::string>>("targets")) {
			m_runProgram += Utils::FromUtf8(target + " ");
			
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
		const auto list = Utils::Win32::GetProcessList();
		std::set_intersection(list.begin(), list.end(), g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end(), std::inserter(pids, pids.end()));
		pids.insert(g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end());
	} else if (g_parameters.m_targetSuffix.empty()) {
		g_parameters.m_targetSuffix.emplace(XivAlex::GameExecutableNameW);
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

void DoPidTask(DWORD pid, const std::filesystem::path& dllDir, const std::filesystem::path& dllPath) {
	auto hProcess = OpenProcessForInjection(pid);
	void* rpModule = hProcess.AddressOf(dllPath, Utils::Win32::Process::ModuleNameCompareMode::FullPath, false);
	const auto path = hProcess.PathOf();

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

	const auto hModule = Utils::Win32::InjectedModule(std::move(hProcess), dllPath);
	auto unloadRequired = false;
	const auto cleanup = Utils::CallOnDestruction([&hModule, &unloadRequired]() {
		if (unloadRequired)
			hModule.Call("EnableXivAlexander", 0, "EnableXivAlexander(0)");
		});

	if (loaderAction == LoaderAction::Load) {
		unloadRequired = true;
		if (const auto loadResult = hModule.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)"); loadResult != 0)
			throw std::runtime_error(std::format("Failed to start the addon: exit code {}", loadResult));
		else
			unloadRequired = false;
	} else if (loaderAction == LoaderAction::Unload) {
		unloadRequired = true;
	}
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

extern "C" __declspec(dllimport) int __stdcall EnableInjectOnCreateProcess(size_t bEnable);

int RunProgram(const std::filesystem::path& path) {
	STARTUPINFOW si{};
	si.cb = sizeof si;
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		throw Utils::Win32::Error("CreateProcessW({})", path);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return 0;
}

int RunLauncher() {
	try {
		EnableInjectOnCreateProcess(1);

		if (!g_parameters.m_runProgram.empty())
			return RunProgram(g_parameters.m_runProgram);
		
		const auto launchers = XivAlex::FindGameLaunchers();
		switch (g_parameters.m_launcherType) {
			case LauncherType::Auto:
				for (const auto& it : launchers) {
					for (const auto& it2 : it.second.AlternativeBoots)
						return RunProgram(it2.second);
					return RunProgram(it.second.BootApp);
				}
				throw std::runtime_error("No suitable ffxiv installation detected. Please specify path.");

			case LauncherType::International:
			{
				const auto launcher = launchers.at(XivAlex::GameRegion::International);
				for (const auto& it : launcher.AlternativeBoots)
					return RunProgram(it.second);
				return RunProgram(launcher.BootApp);
			}
			case LauncherType::Korean:
			{
				const auto launcher = launchers.at(XivAlex::GameRegion::Korean);
				SetEnvironmentVariableW(L"__COMPAT_LAYER", L"RunAsInvoker");
				return RunProgram(launcher.BootApp);
			}
			case LauncherType::Chinese:
			{
				const auto launcher = launchers.at(XivAlex::GameRegion::Chinese);
				SetEnvironmentVariableW(L"__COMPAT_LAYER", L"RunAsInvoker");
				return RunProgram(launcher.BootApp);
			}
		}
	} catch (std::out_of_range&) {
		if (g_parameters.m_action == LoaderAction::Auto)
			MessageBoxW(nullptr, L"No running FFXIV process or installation detected.", MsgboxTitle, MB_OK | MB_ICONINFORMATION);
		else
			MessageBoxW(nullptr, L"No FFXIV installation cold be detected. Please specify launcher path.", MsgboxTitle, MB_OK | MB_ICONINFORMATION);
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
	try {
		g_parameters.Parse(lpCmdLine);
	} catch (std::exception& err) {
		Utils::Win32::MessageBoxF(nullptr, MB_ICONWARNING, MsgboxTitle, L"Faild to parse command line arguments.\n\n{}\n\n{}", err.what(), g_parameters.GetHelpMessage());
		return -1;
	}
	if (g_parameters.m_help) {
		MessageBoxW(nullptr, g_parameters.GetHelpMessage().c_str(), MsgboxTitle, MB_OK);
		return 0;
	}
	if (g_parameters.m_web) {
		ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander", nullptr, nullptr, SW_SHOW);
		return 0;
	}

	const auto dllDir = Utils::Win32::Process::Current().PathOf().parent_path();
	const auto dllPath = dllDir / XivAlex::XivAlexDllNameW;
	Utils::Win32::Closeable::LoadedModule hModule;

	try {
		hModule = Utils::Win32::Closeable::LoadedModule(LoadLibraryW(dllPath.c_str()), nullptr, "Failed to load XivAlexander.dll");
		CheckDllVersion(hModule);
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
			auto process = Utils::Win32::Process();
			process.Attach(reinterpret_cast<HANDLE>(static_cast<size_t>(val)), true);
			return PatchEntryPointForInjection(process);
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
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, L"{}", errors);
		}
		return -1;
	}

	if (!g_parameters.m_disableAutoRunAs && !Utils::Win32::IsUserAnAdmin() && RequiresAdminAccess(pids)) {
		SHELLEXECUTEINFOW si = {};
		const auto path = Utils::Win32::Process::Current().PathOf();
		si.cbSize = sizeof si;
		si.lpVerb = L"runas";
		si.lpFile = path.c_str();
		si.lpParameters = lpCmdLine;
		si.nShow = nShowCmd;
		if (ShellExecuteExW(&si))
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
