#include "pch.h"

#include "WinPath.h"

static void* GetModulePointer(HANDLE hProcess, const wchar_t* sDllPath) {
	Utils::Win32Handle th32(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(hProcess)),
		INVALID_HANDLE_VALUE,
		"Failed to list modules of process %d.", GetProcessId(hProcess));
	MODULEENTRY32W mod{ sizeof MODULEENTRY32W };
	if (!Module32FirstW(th32, &mod))
		return nullptr;

	do {
		if (_wcsicmp(mod.szExePath, sDllPath) == 0)
			return mod.modBaseAddr;
	} while (Module32NextW(th32, &mod));
	return nullptr;
}

static std::wstring GetProcessExecutablePath(HANDLE hProcess) {
	std::wstring sPath(PATHCCH_MAX_CCH, L'\0');
	while (true) {
		auto length = static_cast<DWORD>(sPath.size());
		if (!QueryFullProcessImageNameW(hProcess, 0, &sPath[0], &length))
			throw Utils::WindowsError("GetProcessExecutablePath:QueryFullProcessImageNameW");
		if (length < sPath.size() - 1) {
			sPath.resize(length);
			break;
		}
	}
	return sPath;
}

static int CallRemoteFunction(HANDLE hProcess, void* rpfn, void* rpParam, const char* pcszDescription) {
	const Utils::Win32Handle hLoadLibraryThread(CreateRemoteThread(hProcess, nullptr, 0, static_cast<LPTHREAD_START_ROUTINE>(rpfn), rpParam, 0, nullptr),
		Utils::NullHandle,
		"Failed to call remote function %s@%p(%p)", pcszDescription, rpfn, rpParam);
	WaitForSingleObject(hLoadLibraryThread, INFINITE);
	DWORD exitCode;
	GetExitCodeThread(hLoadLibraryThread, &exitCode);
	return exitCode;
}

static int InjectDll(HANDLE hProcess, const wchar_t* pszDllPath) {
	const size_t nDllPathLength = wcslen(pszDllPath) + 1;

	if (GetModulePointer(hProcess, pszDllPath))
		return 0;

	const auto nNumberOfBytes = nDllPathLength * sizeof pszDllPath[0];

	void* rpszDllPath = VirtualAllocEx(hProcess, nullptr, nNumberOfBytes, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!rpszDllPath)
		return -1;

	if (!WriteProcessMemory(hProcess, rpszDllPath, pszDllPath, nNumberOfBytes, nullptr)) {
		VirtualFreeEx(hProcess, rpszDllPath, 0, MEM_RELEASE);
		return -1;
	}

	const auto exitCode = CallRemoteFunction(hProcess, LoadLibraryW, rpszDllPath, "LoadLibraryW");
	VirtualFreeEx(hProcess, rpszDllPath, 0, MEM_RELEASE);
	return exitCode;
}

BOOL SetPrivilege(HANDLE hToken, LPCTSTR Privilege, BOOL bEnablePrivilege) {
	TOKEN_PRIVILEGES tp;
	LUID luid;
	TOKEN_PRIVILEGES tpPrevious;
	DWORD cbPrevious = sizeof(TOKEN_PRIVILEGES);

	if (!LookupPrivilegeValue(NULL, Privilege, &luid)) return FALSE;

	// 
	// first pass.  get current privilege setting
	// 
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = 0;

	AdjustTokenPrivileges(
		hToken,
		FALSE,
		&tp,
		sizeof(TOKEN_PRIVILEGES),
		&tpPrevious,
		&cbPrevious
	);

	if (GetLastError() != ERROR_SUCCESS) return FALSE;

	// 
	// second pass.  set privilege based on previous setting
	// 
	tpPrevious.PrivilegeCount = 1;
	tpPrevious.Privileges[0].Luid = luid;

	if (bEnablePrivilege)
		tpPrevious.Privileges[0].Attributes |= (SE_PRIVILEGE_ENABLED);
	else
		tpPrevious.Privileges[0].Attributes ^= (SE_PRIVILEGE_ENABLED & tpPrevious.Privileges[0].Attributes);

	AdjustTokenPrivileges(
		hToken, FALSE, &tpPrevious, cbPrevious, NULL, NULL);

	if (GetLastError() != ERROR_SUCCESS) return FALSE;

	return TRUE;
}

void AddDebugPrivilege() {
	Utils::Win32Handle token;
	{
		HANDLE hToken;
		if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
			if (GetLastError() == ERROR_NO_TOKEN) {
				if (!ImpersonateSelf(SecurityImpersonation))
					throw Utils::WindowsError("AddDebugPrivilege: ImpersonateSelf(SecurityImpersonation)");

				if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken))
					throw Utils::WindowsError("AddDebugPrivilege: OpenThreadToken#2");
			} else
				throw Utils::WindowsError("AddDebugPrivilege: OpenThreadToken#1");
		}
		token = Utils::Win32Handle(hToken, INVALID_HANDLE_VALUE, "AddDebugPrivilege: Invalid");
	}

	if (!SetPrivilege(token, SE_DEBUG_NAME, TRUE))
		throw Utils::WindowsError("AddDebugPrivilege/SetPrivilege(SeDebugPrivilege)");
}

void* FindModuleAddress(HANDLE hProcess, const Utils::WinPath& szDllPath) {
	std::vector<HMODULE> hMods;
	DWORD cbNeeded;
	do {
		hMods.resize(hMods.size() + std::min<size_t>(1024, std::max<size_t>(32768, hMods.size())));
		cbNeeded = static_cast<DWORD>(hMods.size());
		if (!EnumProcessModules(hProcess, &hMods[0], static_cast<DWORD>(hMods.size() * sizeof(HMODULE)), &cbNeeded))
			throw Utils::WindowsError(FormatString("FindModuleAdderss(pid=%d, path=%s)/EnumProcessModules", GetProcessId(hProcess), szDllPath));
	} while (cbNeeded == hMods.size() * sizeof(HMODULE));
	hMods.resize(cbNeeded / sizeof(HMODULE));

	std::wstring sModName;
	sModName.resize(PATHCCH_MAX_CCH);
	for (const auto hMod : hMods) {
		const auto remoteModulePath = Utils::WinPath(hMod, hProcess);
		if (remoteModulePath == szDllPath)
			return hMod;
	}
	return nullptr;
}

extern "C" __declspec(dllimport) int __stdcall LoadXivAlexander(void* lpReserved);
extern "C" __declspec(dllimport) int __stdcall UnloadXivAlexander(void* lpReserved);

static
std::pair<std::string, std::string> FormatModuleVersionString(HMODULE hModule) {
	const auto hDllVersion = FindResourceW(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
	if (hDllVersion == nullptr)
		throw std::exception("Failed to find version resource.");
	const Utils::Win32Handle<HGLOBAL, FreeResource> hVersionResource(LoadResource(hModule, hDllVersion), 
		nullptr, 
		"FormatModuleVersionString: Failed to load version resource.");
	const auto lpVersionInfo = LockResource(hVersionResource);  // no need to "UnlockResource"

	UINT size = 0;
	LPVOID lpBuffer = nullptr;
	if (!VerQueryValueW(lpVersionInfo, L"\\", &lpBuffer, &size))
		throw std::exception("Failed to query version information.");
	const VS_FIXEDFILEINFO& versionInfo = *static_cast<const VS_FIXEDFILEINFO*>(lpBuffer);
	if (versionInfo.dwSignature != 0xfeef04bd)
		throw std::exception("Invalid version info found.");
	return std::make_pair<>(
		Utils::FormatString("%d.%d.%d.%d",
			(versionInfo.dwFileVersionMS >> 16) & 0xFFFF,
			(versionInfo.dwFileVersionMS >> 0) & 0xFFFF,
			(versionInfo.dwFileVersionLS >> 16) & 0xFFFF,
			(versionInfo.dwFileVersionLS >> 0) & 0xFFFF),
		Utils::FormatString("%d.%d.%d.%d",
			(versionInfo.dwProductVersionMS >> 16) & 0xFFFF,
			(versionInfo.dwProductVersionMS >> 0) & 0xFFFF,
			(versionInfo.dwProductVersionLS >> 16) & 0xFFFF,
			(versionInfo.dwProductVersionLS >> 0) & 0xFFFF));
}

static
void CheckDllVersion(const wchar_t* szDllPath) {
	const auto hDll = GetModuleHandleW(szDllPath);
	if (!hDll)
		throw std::exception("XivAlexander.dll not found.");
	auto [dllFileVersion, dllProductVersion] = FormatModuleVersionString(hDll);
	auto [selfFileVersion, selfProductVersion] = FormatModuleVersionString(GetModuleHandleW(nullptr));

	if (dllFileVersion != selfFileVersion)
		throw std::exception(Utils::FormatString("File versions do not match. (XivAlexanderLoader.exe: %s, XivAlexander.dll: %s)",
			selfFileVersion.c_str(), dllFileVersion.c_str()).c_str());

	if (dllProductVersion != selfProductVersion)
		throw std::exception(Utils::FormatString("Product versions do not match. (XivAlexanderLoader.exe: %s, XivAlexander.dll: %s)",
			selfProductVersion.c_str(), selfFileVersion.c_str()).c_str());
}

class XivAlexanderLoaderParameter {
public:
	argparse::ArgumentParser argp;

	enum class DefaultAction {
		Ask,
		Load,
		Unload,
	} m_action = DefaultAction::Ask;
	bool m_quiet = false;
	bool m_help = false;
	bool m_web = false;
	std::vector<int> m_targetPids{};
	std::vector<std::wstring> m_targetSuffix{};

	XivAlexanderLoaderParameter()
		: argp("XivAlexanderLoader") {

		argp.add_argument("-a", "--action")
			.help("specifies default action for each process (possible values: ask, load, unload)")
			.required()
			.nargs(1)
			.default_value(DefaultAction::Ask)
			.action([](std::string val) {
				std::wstring valw = Utils::FromUtf8(val);
				CharLowerW(&valw[0]);
				val = Utils::ToUtf8(valw);
				if (val == "ask")
					return DefaultAction::Ask;
				else if (val == "load")
					return DefaultAction::Load;
				else if (val == "unload")
					return DefaultAction::Unload;
				else
					throw std::exception("Invalid parameter given for action parameter.");
				});
		argp.add_argument("-q", "--quiet")
			.help("disables error messages")
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("--web")
			.help("opens github repository at https://github.com/Soreepeong/XivAlexander and exit")
			.default_value(false)
			.implicit_value(true);
		argp.add_argument("targets")
			.help("list of target process ID or path suffix.")
			.default_value(std::vector<std::string>())
			.remaining()
			.action([](std::string val) {
				std::wstring valw = Utils::FromUtf8(val);
				CharLowerW(&valw[0]);
				return Utils::ToUtf8(valw);
			});
	}

	void Parse(LPWSTR lpCmdLine) {
		std::vector<std::string> args;

		wchar_t szModulePath[PATHCCH_MAX_CCH] = { 0 };
		GetModuleFileNameW(nullptr, szModulePath, _countof(szModulePath));
		args.push_back(Utils::ToUtf8(szModulePath));

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

		m_action = argp.get<DefaultAction>("-a");
		m_quiet = argp.get<bool>("-q");
		m_web = argp.get<bool>("--web");
		
		for (const auto& target : argp.get<std::vector<std::string>>("targets")) {
			size_t idx = 0;
			int pid = -1;

			try {
				pid = std::stoi(target, &idx);
			} catch (std::invalid_argument&) {
				// empty
			} catch (std::out_of_range&) {
				// empty
			}
			if (pid == -1 || idx != target.length())
				m_targetSuffix.push_back(Utils::FromUtf8(target));
			else
				m_targetPids.push_back(pid);
		}
	}

	std::wstring GetHelpMessage() const {
		return Utils::FormatString(L"XivAlexanderLoader: loads XivAlexander into game process (DirectX 11 version, x64 only).\n\n%s",
			Utils::FromUtf8(argp.help().str()).c_str()
			);
	}
} g_parameters;

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	const auto MsgboxTitle = L"XivAlexander Loader";
	
	try {
		g_parameters.Parse(lpCmdLine);
	} catch (std::exception& err) {
		const auto what = Utils::FromUtf8(err.what());
		const auto help = g_parameters.GetHelpMessage();
		const auto msg = std::wstring(L"Failed to parse command line arguments.\n\n") + what + L"\n\n" + help;
		MessageBoxW(nullptr, msg.c_str(), MsgboxTitle, MB_OK | MB_ICONWARNING);
		return -1;
	}
	if (g_parameters.m_help){
		MessageBoxW(nullptr, g_parameters.GetHelpMessage().c_str(), MsgboxTitle, MB_OK);
		return 0;
	}
	if (g_parameters.m_web) {
		ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander", nullptr, nullptr, SW_SHOW);
		return 0;
	}

	DWORD pid;
	HWND hwnd = nullptr;
	const auto dllDir = Utils::WinPath(nullptr).RemoveComponentInplace();
	const auto dllPath = Utils::WinPath(dllDir, L"XivAlexander.dll");
	
	try {
		CheckDllVersion(dllPath);
	} catch (std::exception& e) {
		if (MessageBoxW(nullptr, 
			Utils::FormatString(
				L"Failed to verify XivAlexander.dll and XivAlexanderLoader.exe have the matching versions (%s).\n\nDo you want to download again from Github?",
				Utils::FromUtf8(e.what()).c_str()
			).c_str(),
			MsgboxTitle, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1) == IDYES) {
			ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander/releases", nullptr, nullptr, SW_SHOW);
		}
		return -1;
	}

	std::string debugPrivilegeError = "OK.";
	try {
		AddDebugPrivilege();
	} catch (const std::exception& err) {
		debugPrivilegeError = Utils::FormatString("Failed to obtain.\n* Try running this program as Administrator.\n* %s", err.what());
	}

	const std::wstring ProcessName(L"ffxiv_dx11.exe");

	bool found = false;
	
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		GetWindowThreadProcessId(hwnd, &pid);

		std::wstring sExePath;
		
		try {
			{
				Utils::Win32Handle hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid),
					Utils::NullHandle,
					"OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, %d)", pid);
				
				sExePath = GetProcessExecutablePath(hProcess);
			}
			if (g_parameters.m_targetPids.empty() && g_parameters.m_targetSuffix.empty()) {
				if (sExePath.length() < ProcessName.length() || (0 != sExePath.compare(sExePath.length() - ProcessName.length(), ProcessName.length(), ProcessName)))
					continue;
			} else if (std::find(g_parameters.m_targetPids.begin(), g_parameters.m_targetPids.end(), pid) == g_parameters.m_targetPids.end()) {
				bool suffixFound = false;
				auto sExePathLowercase = sExePath;
				CharLowerW(&sExePathLowercase[0]);
				for (const auto& suffix : g_parameters.m_targetSuffix) {
					if (sExePathLowercase.length() >= suffix.length() && (0 == sExePathLowercase.compare(sExePathLowercase.length() - suffix.length(), suffix.length(), suffix)))
						suffixFound = true;
				}
				if (!suffixFound)
					continue;
			}

			found = true;
			
			void* rpModule;
			{
				Utils::Win32Handle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid),
					Utils::NullHandle,
					"OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, false, %d)", pid);
				rpModule = FindModuleAddress(hProcess, dllPath);
			}

			std::wstring msg;
			UINT nMsgType;
			if (rpModule) {
				msg = Utils::FormatString(
					L"XivAlexander detected in FFXIV Process (%d:%s)\n"
					L"Press Yes to try loading again if it hasn't loaded properly,\n"
					L"Press No to skip,\n"
					L"Press Cancel to unload.\n"
					L"\n"
					L"Note: your anti-virus software will probably classify DLL injection as a malicious action, "
					L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.",
					static_cast<int>(pid), sExePath.c_str());
				nMsgType = (MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
			} else {
				const auto regionAndVersion = Utils::ResolveGameReleaseRegion(sExePath);
				const auto gameConfigFilename = Utils::FormatString(L"game.%s.%s.json", 
					std::get<0>(regionAndVersion).c_str(),
					std::get<1>(regionAndVersion).c_str());
				const auto gameConfigPath = Utils::WinPath(dllDir, gameConfigFilename);

				if (!g_parameters.m_quiet && !gameConfigPath.Exists()) {
					msg = Utils::FormatString(
						L"FFXIV Process found:\n"
						L"* PID: %d\n"
						L"* Path: %s\n"
						L"* Game Version Configuration File: %s\n"
						L"Continue loading XivAlexander into this process?\n"
						L"\n"
						L"Notes\n"
						L"* Corresponding game version configuration file for this process does not exist. "
						L"You may want to check your game installation path, and edit the right entry in the above file first.\n"
						L"* Your anti-virus software will probably classify DLL injection as a malicious action, "
						L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.",
						static_cast<int>(pid), sExePath.c_str(), gameConfigFilename.c_str(),
						std::get<0>(regionAndVersion).c_str(),
						std::get<1>(regionAndVersion).c_str());
					nMsgType = (MB_YESNO | MB_DEFBUTTON1 | MB_ICONWARNING);
				} else {
					msg = Utils::FormatString(
						L"FFXIV Process found:\n"
						L"* Process ID: %d\n"
						L"* Path: %s\n"
						L"* Game Version Configuration File: %s\n"
						L"Continue loading XivAlexander into this process?\n"
						L"\n"
						L"Note: your anti-virus software will probably classify DLL injection as a malicious action, "
						L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.",
						static_cast<int>(pid), sExePath.c_str(), gameConfigFilename.c_str());
					nMsgType = (MB_YESNO | MB_DEFBUTTON1);
				}
			}

			int response;
			if (g_parameters.m_action == XivAlexanderLoaderParameter::DefaultAction::Ask)
				response = MessageBoxW(nullptr, msg.c_str(), MsgboxTitle, nMsgType);
			else if (g_parameters.m_action == XivAlexanderLoaderParameter::DefaultAction::Load)
				response = IDYES;
			else if (g_parameters.m_action == XivAlexanderLoaderParameter::DefaultAction::Unload)
				response = IDCANCEL;
			else
				throw std::exception("Unreachable");

			if (response == IDNO)
				continue;

			{
				Utils::Win32Handle hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid),
					Utils::NullHandle,
					"OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, %d)", pid);

				rpModule = FindModuleAddress(hProcess, dllPath);
				if (response == IDCANCEL && !rpModule)
					continue;

				if (!rpModule) {
					InjectDll(hProcess, dllPath);
					rpModule = FindModuleAddress(hProcess, dllPath);
				}

				DWORD loadResult = 0;
				if (response == IDYES) {
					loadResult = CallRemoteFunction(hProcess, LoadXivAlexander, nullptr, "LoadXivAlexander");
					if (loadResult != 0) {
						response = IDCANCEL;
					}
				}
				if (response == IDCANCEL) {
					if (CallRemoteFunction(hProcess, UnloadXivAlexander, nullptr, "UnloadXivAlexander")) {
						CallRemoteFunction(hProcess, FreeLibrary, rpModule, "FreeLibrary");
					}
				}
				if (loadResult)
					throw std::exception(Utils::FormatString("Failed to start the addon: %d", loadResult).c_str());
			}
		} catch (std::exception& e) {
			if (!g_parameters.m_quiet)
				MessageBoxW(nullptr, Utils::FromUtf8(Utils::FormatString(
					"Process ID: %d\n"
					"\n"
					"Debug Privilege: %s\n"
					"\n"
					"Error:\n"
					"* %s", 
					pid,
					debugPrivilegeError.c_str(), 
					e.what()
				)).c_str(), MsgboxTitle, MB_OK | MB_ICONERROR);
		}
	}

	if (!found && !g_parameters.m_quiet) {
		if (g_parameters.m_targetPids.empty() && g_parameters.m_targetSuffix.empty()) {
			MessageBoxW(nullptr, L"ffxiv_dx11.exe not found. Run the game first, and then try again.", MsgboxTitle, MB_OK | MB_ICONERROR);
		} else {
			MessageBoxW(nullptr, L"No matching process found.", MsgboxTitle, MB_OK | MB_ICONERROR);
		}
	}
	return 0;
}
