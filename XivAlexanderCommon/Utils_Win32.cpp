#include "pch.h"
#include "Utils_Win32.h"
#include "Utils_Win32_Closeable.h"

std::string Utils::Win32::FormatWindowsErrorMessage(unsigned int errorCode) {
	std::set<std::string> messages;
	for (const auto langId : {
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
#ifdef _DEBUG
		MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN),
		MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN),
#endif
		}) {
		LPTSTR errorText = nullptr;
		FormatMessage(
			FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			errorCode,
			langId,
			reinterpret_cast<LPTSTR>(&errorText), // output 
			0, // minimum size for output buffer
			nullptr); // arguments - see note 
		if (nullptr != errorText) {
			messages.insert(ToUtf8(errorText));
			LocalFree(errorText);
		}
	}
	std::string res;
	for (const auto& message : messages) {
		if (!res.empty())
			res += " / ";
		res += message;
	}
	return res;
}

std::pair<std::string, std::string> Utils::Win32::FormatModuleVersionString(HMODULE hModule) {
	const auto hDllVersion = FindResourceW(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
	if (hDllVersion == nullptr)
		throw std::runtime_error("Failed to find version resource.");
	const auto hVersionResource = Closeable::GlobalResource(LoadResource(hModule, hDllVersion),
	                                                        nullptr,
	                                                        "FormatModuleVersionString: Failed to load version resource.");
	const auto lpVersionInfo = LockResource(hVersionResource);  // no need to "UnlockResource"

	UINT size = 0;
	LPVOID lpBuffer = nullptr;
	if (!VerQueryValueW(lpVersionInfo, L"\\", &lpBuffer, &size))
		throw std::runtime_error("Failed to query version information.");
	const VS_FIXEDFILEINFO& versionInfo = *static_cast<const VS_FIXEDFILEINFO*>(lpBuffer);
	if (versionInfo.dwSignature != 0xfeef04bd)
		throw std::runtime_error("Invalid version info found.");
	return std::make_pair<>(
		std::format("{}.{}.{}.{}",
		             (versionInfo.dwFileVersionMS >> 16) & 0xFFFF,
		             (versionInfo.dwFileVersionMS >> 0) & 0xFFFF,
		             (versionInfo.dwFileVersionLS >> 16) & 0xFFFF,
		             (versionInfo.dwFileVersionLS >> 0) & 0xFFFF),
		std::format("{}.{}.{}.{}",
		             (versionInfo.dwProductVersionMS >> 16) & 0xFFFF,
		             (versionInfo.dwProductVersionMS >> 0) & 0xFFFF,
		             (versionInfo.dwProductVersionLS >> 16) & 0xFFFF,
		             (versionInfo.dwProductVersionLS >> 0) & 0xFFFF));
}

BOOL Utils::Win32::EnableTokenPrivilege(HANDLE hToken, LPCTSTR Privilege, BOOL bEnablePrivilege) {
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

void Utils::Win32::SetThreadDescription(HANDLE hThread, const std::wstring& description) {
	typedef HRESULT(WINAPI* SetThreadDescriptionT)(
		_In_ HANDLE hThread,
		_In_ PCWSTR lpThreadDescription
		);
	SetThreadDescriptionT pfnSetThreadDescription = nullptr;

	if (const auto hMod = Closeable::LoadedModule(LoadLibraryExW(L"kernel32.dll", Closeable::Handle::Null, LOAD_LIBRARY_SEARCH_SYSTEM32), nullptr))
		pfnSetThreadDescription = reinterpret_cast<SetThreadDescriptionT>(GetProcAddress(hMod, "SetThreadDescription"));
	else if (const auto hMod = Closeable::LoadedModule(LoadLibraryExW(L"KernelBase.dll", Closeable::Handle::Null, LOAD_LIBRARY_SEARCH_SYSTEM32), nullptr))
		pfnSetThreadDescription = reinterpret_cast<SetThreadDescriptionT>(GetProcAddress(hMod, "SetThreadDescription"));

	if (pfnSetThreadDescription)
		pfnSetThreadDescription(hThread, description.data());
}

void Utils::Win32::SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked) {
	MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };
	mii.fMask = MIIM_STATE;

	GetMenuItemInfoW(hMenu, nMenuId, false, &mii);
	if (bChecked)
		mii.fState |= MFS_CHECKED;
	else
		mii.fState &= ~MFS_CHECKED;
	SetMenuItemInfoW(hMenu, nMenuId, false, &mii);
}

void Utils::Win32::SetMenuState(HWND hWnd, DWORD nMenuId, bool bChecked) {
	SetMenuState(GetMenu(hWnd), nMenuId, bChecked);
}

void Utils::Win32::AddDebugPrivilege() {
	Closeable::Handle token;
	{
		HANDLE hToken;
		if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken)) {
			if (GetLastError() == ERROR_NO_TOKEN) {
				if (!ImpersonateSelf(SecurityImpersonation))
					throw Error("AddDebugPrivilege: ImpersonateSelf(SecurityImpersonation)");

				if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &hToken))
					throw Error("AddDebugPrivilege: OpenThreadToken#2");
			} else
				throw Error("AddDebugPrivilege: OpenThreadToken#1");
		}
		token = Closeable::Handle(hToken, INVALID_HANDLE_VALUE, "AddDebugPrivilege: Invalid");
	}

	if (!EnableTokenPrivilege(token, SE_DEBUG_NAME, TRUE))
		throw Error("AddDebugPrivilege/EnableTokenPrivilege(SeDebugPrivilege)");
}

Utils::Win32::Error::Error(int errorCode, const std::string& msg)
	: std::runtime_error(FormatWindowsErrorMessage(errorCode) + ": " + msg)
	, m_nErrorCode(errorCode) {
}

Utils::Win32::Error::Error(const std::string& msg) : Error(GetLastError(), msg) {
}
