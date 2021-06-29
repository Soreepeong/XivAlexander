#include "pch.h"

const auto MsgboxTitle = L"XivAlexander Loader";

namespace W32Modules = Utils::Win32::Modules;

extern "C" __declspec(dllimport) int __stdcall LoadXivAlexander(void* lpReserved);
extern "C" __declspec(dllimport) int __stdcall UnloadXivAlexander(void* lpReserved);

static
void CheckDllVersion(const std::filesystem::path& dllPath) {
	const auto hDll = GetModuleHandleW(dllPath.c_str());
	if (!hDll)
		throw std::runtime_error("XivAlexander.dll not found.");
	auto [dllFileVersion, dllProductVersion] = Utils::Win32::FormatModuleVersionString(hDll);
	auto [selfFileVersion, selfProductVersion] = Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr));

	if (dllFileVersion != selfFileVersion)
		throw std::runtime_error(std::format("File versions do not match. (XivAlexanderLoader.exe: {}, XivAlexander.dll: {})",
			selfFileVersion, dllFileVersion));

	if (dllProductVersion != selfProductVersion)
		throw std::runtime_error(std::format("Product versions do not match. (XivAlexanderLoader.exe: {}, XivAlexander.dll: {})",
			selfProductVersion, selfFileVersion));
}

enum class LoaderAction : int {
	Ask,
	Load,
	Unload,
	Ignore,  // for internal use only
};

template <>
std::string argparse::details::repr(LoaderAction const& val) {
	switch (val) {
		case LoaderAction::Ask: return "ask";
		case LoaderAction::Load: return "load";
		case LoaderAction::Unload: return "unload";
		case LoaderAction::Ignore: return "ignore";
	}
	return std::format("({})", static_cast<int>(val));
}

class XivAlexanderLoaderParameter {
public:
	argparse::ArgumentParser argp;

	LoaderAction m_action = LoaderAction::Ask;
	bool m_quiet = false;
	bool m_help = false;
	bool m_web = false;
	bool m_disableAutoRunAs = true;
	std::set<DWORD> m_targetPids{};
	std::set<std::wstring> m_targetSuffix{};

	XivAlexanderLoaderParameter()
		: argp("XivAlexanderLoader") {

		argp.add_argument("-a", "--action")
			.help("specify default action for each process (possible values: ask, load, unload)")
			.required()
			.nargs(1)
			.default_value(LoaderAction::Ask)
			.action([](const std::string& val) {
			auto valw = Utils::FromUtf8(val);
			CharLowerW(&valw[0]);
			if (valw == L"ask")
				return LoaderAction::Ask;
			else if (valw == L"load")
				return LoaderAction::Load;
			else if (valw == L"unload")
				return LoaderAction::Unload;
			else
				throw std::runtime_error("Invalid parameter given for action parameter.");
				});
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
		argp.add_argument("targets")
			.help("list of target process ID or path suffix.")
			.default_value(std::vector<std::string>())
			.remaining()
			.action([](const std::string& val) {
			auto valw = Utils::FromUtf8(val);
			CharLowerW(&valw[0]);
			return Utils::ToUtf8(valw);
				});
	}

	void Parse(LPWSTR lpCmdLine) {
		std::vector<std::string> args;

		args.push_back(Utils::ToUtf8(W32Modules::PathFromModule()));

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
		m_quiet = argp.get<bool>("-q");
		m_web = argp.get<bool>("--web");
		m_disableAutoRunAs = argp.get<bool>("-d");

		for (const auto& target : argp.get<std::vector<std::string>>("targets")) {
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
		const auto list = W32Modules::GetProcessList();
		std::set_intersection(list.begin(), list.end(), g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end(), std::inserter(pids, pids.end()));
		pids.insert(g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end());
	} else if (g_parameters.m_targetSuffix.empty()) {
		g_parameters.m_targetSuffix.emplace(L"ffxiv_dx11.exe");
	}
	if (!g_parameters.m_targetSuffix.empty()) {
		for (const auto pid : W32Modules::GetProcessList()) {
			try {
				auto hProcess = Utils::Win32::Closeable::Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid),
					Utils::Win32::Closeable::Handle::Null,
					"OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, {})", pid);
				const auto path = W32Modules::PathFromModule(nullptr, hProcess);
				auto buf = path.wstring();
				auto suffixFound = false;
				CharLowerW(&buf[0]);
				for (const auto& suffix : g_parameters.m_targetSuffix) {
					if ((suffixFound = buf.ends_with(suffix)))
						break;
				}
				if (suffixFound)
					pids.insert(pid);
			} catch (std::runtime_error&) {
				// uninterested
			}
		}
	}
	return pids;
}

auto OpenProcessForInjection(DWORD pid) {
	return Utils::Win32::Closeable::Handle(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid),
		Utils::Win32::Closeable::Handle::Null,
		"OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, {})", pid);
}

void DoPidTask(DWORD pid, const std::filesystem::path& dllDir, const std::filesystem::path& dllPath) {
	const auto hProcess = OpenProcessForInjection(pid);
	void* rpModule = W32Modules::FindModuleAddress(hProcess, dllPath);
	const auto path = W32Modules::PathFromModule(nullptr, hProcess);

	auto loaderAction = g_parameters.m_action;
	if (loaderAction == LoaderAction::Ask) {
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
					loaderAction = LoaderAction::Ignore;
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
						loaderAction = LoaderAction::Ignore;
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
						loaderAction = LoaderAction::Ignore;
				}
			}
		}
	}

	if (loaderAction == LoaderAction::Ignore)
		return;

	if (loaderAction == LoaderAction::Unload && !rpModule)
		return;

	rpModule = W32Modules::InjectDll(hProcess, dllPath);
	auto unloadRequired = false;
	const auto cleanup = Utils::CallOnDestruction([&hProcess, rpModule, &unloadRequired]() {
		if (unloadRequired)
			W32Modules::CallRemoteFunction(hProcess, UnloadXivAlexander, nullptr, "UnloadXivAlexander");
		W32Modules::CallRemoteFunction(hProcess, FreeLibrary, rpModule, "FreeLibrary");
		});

	if (loaderAction == LoaderAction::Load) {
		unloadRequired = true;
		if (const auto loadResult = W32Modules::CallRemoteFunction(hProcess, LoadXivAlexander, nullptr, "LoadXivAlexander"); loadResult != 0)
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

	const auto dllDir = W32Modules::PathFromModule().parent_path();
	const auto dllPath = dllDir / L"XivAlexander.dll";

	try {
		CheckDllVersion(dllPath);
	} catch (std::exception& e) {
		if (Utils::Win32::MessageBoxF(nullptr, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1, MsgboxTitle,
			L"Failed to verify XivAlexander.dll and XivAlexanderLoader.exe have the matching versions ({}).\n\nDo you want to download again from Github?",
			Utils::FromUtf8(e.what())
		) == IDYES) {
			ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander/releases", nullptr, nullptr, SW_SHOW);
		}
		return -1;
	}

	std::string debugPrivilegeError = "OK.";
	try {
		Utils::Win32::AddDebugPrivilege();
	} catch (const std::exception& err) {
		debugPrivilegeError = std::format("Failed to obtain.\n* Try running this program as Administrator.\n* {}", err.what());
	}

	const auto pids = GetTargetPidList();

	if (pids.empty()) {
		if (!g_parameters.m_quiet) {
			std::wstring errors;
			if (g_parameters.m_targetPids.empty() && g_parameters.m_targetSuffix.empty())
				errors = L"ffxiv_dx11.exe not found. Run the game first, and then try again.";
			else
				errors = L"No matching process found.";
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, MsgboxTitle, L"{}", errors);
		}
		return -1;
	}
	
	if (!g_parameters.m_disableAutoRunAs && !Utils::Win32::IsUserAnAdmin() && RequiresAdminAccess(pids)) {
		SHELLEXECUTEINFOW si = {};
		const auto path = Utils::Win32::Modules::PathFromModule();
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
