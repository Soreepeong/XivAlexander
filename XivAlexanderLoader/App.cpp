#include "pch.h"

static void* GetModulePointer(HANDLE hProcess, const wchar_t* sDllPath) {
	Utils::Win32Handle<> th32(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(hProcess)));
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
		QueryFullProcessImageNameW(hProcess, 0, &sPath[0], &length);
		if (length < sPath.size() - 1) {
			sPath.resize(length);
			break;
		}
	}
	return sPath;
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

	Utils::Win32Handle<> hLoadLibraryThread(CreateRemoteThread(hProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryW), rpszDllPath, 0, nullptr));
	WaitForSingleObject(hLoadLibraryThread, INFINITE);
	DWORD exitCode;
	GetExitCodeThread(hLoadLibraryThread, &exitCode);
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

const char* AddDebugPrivilege() {
	Utils::Win32Handle<> token;
	{
		HANDLE hToken;
		if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
			if (GetLastError() == ERROR_NO_TOKEN) {
				if (!ImpersonateSelf(SecurityImpersonation))
					return "ImpersonateSelf";

				if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
					return "OpenThreadToken2";
				}
			} else
				return "OpenThreadToken1";
		}
		token = hToken;
	}

	if (!SetPrivilege(token, SE_DEBUG_NAME, TRUE))
		return "SetPrivilege";
	return nullptr;
}

void* FindModuleAddress(HANDLE hProcess, LPWSTR szDllPath) {
	HMODULE hMods[1024];
	DWORD cbNeeded;
	unsigned int i;
	bool skip = false;
	if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
		for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
			WCHAR szModName[MAX_PATH];
			if (GetModuleFileNameExW(hProcess, hMods[i], szModName, MAX_PATH)) {
				if (wcsncmp(szModName, szDllPath, MAX_PATH) == 0)
					return hMods[i];
			}
		}
	}
	return nullptr;
}

extern "C" __declspec(dllimport) int __stdcall LoadXivAlexander(void* lpReserved);

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
) {
	struct {
		bool noask = false;
		bool noerror = false;
	} params;
	int nArgs;
	LPWSTR *szArgList = CommandLineToArgvW(lpCmdLine, &nArgs);
	if (nArgs > 1) {
		for (int i = 1; i < nArgs; i++) {
			if (wcsncmp(L"/noask", szArgList[i], 6) == 0)
				params.noask = true;
			if (wcsncmp(L"/noerror", szArgList[i], 8) == 0)
				params.noerror = true;
		}
	}
	LocalFree(szArgList);

	DWORD pid;
	HWND hwnd = nullptr;
	wchar_t szDllPath[PATHCCH_MAX_CCH] = { 0 };
	GetModuleFileNameW(nullptr, szDllPath, _countof(szDllPath));
	PathCchRemoveFileSpec(szDllPath, _countof(szDllPath));
	PathCchAppend(szDllPath, _countof(szDllPath), L"XivAlexander.dll");

	const auto szDebugPrivError = AddDebugPrivilege();

	const std::wstring ProcessName(L"ffxiv_dx11.exe");

	bool found = false;
	
	while (hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr)) {
		GetWindowThreadProcessId(hwnd, &pid);

		std::wstring sExePath;

		try {
			{
				Utils::Win32Handle<> hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid));
				sExePath = GetProcessExecutablePath(hProcess);
			}
			if (sExePath.length() < ProcessName.length() || (0 != sExePath.compare(sExePath.length() - ProcessName.length(), ProcessName.length(), ProcessName)))
				continue;

			{
				Utils::Win32Handle<> hProcess(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid));
				if (FindModuleAddress(hProcess, szDllPath))
					continue;
			}
			
			found = true;
			
			if (!params.noask && MessageBoxW(nullptr, Utils::FormatString(
				L"FFXIV Process found: %s\nProceeed loading into the process?\n\n"
				L"Note: your anti-virus software will probably classify DLL injection as a malicious action, "
				L"and you will have to add both XivAlexanderLoader.exe and XivAlexander.dll to exceptions.", 
				sExePath.c_str()).c_str(), L"XivAlexander Loader", MB_YESNO) != IDYES)
				continue;

			{
				Utils::Win32Handle<> hProcess(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, false, pid));
				InjectDll(hProcess, szDllPath);
				
				void* rpModule = FindModuleAddress(hProcess, szDllPath);

				Utils::Win32Handle<> hLoadXivAlexanderThread(CreateRemoteThread(hProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadXivAlexander), nullptr, 0, nullptr));
				WaitForSingleObject(hLoadXivAlexanderThread, INFINITE);
				DWORD exitCode;
				GetExitCodeThread(hLoadXivAlexanderThread, &exitCode);
			}
		} catch (std::exception& e) {
			if (!params.noerror)
				MessageBoxW(nullptr, Utils::FromUtf8(Utils::FormatString("PID %d: %s\nDebug Privilege: %s", pid, e.what(), szDebugPrivError ? szDebugPrivError : "OK")).c_str(), L"Error", MB_OK | MB_ICONERROR);
		}
	}

	if (!found) {
		if (!params.noerror)
			MessageBoxW(nullptr, L"ffxiv_dx11.exe not found", L"Error", MB_OK | MB_ICONERROR);
	}
	return 0;
}