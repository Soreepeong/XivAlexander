#include "pch.h"
#include "Utils_Win32_Resource.h"

Utils::Win32::GlobalResource::GlobalResource(HINSTANCE hInstance, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage, bool fallbackToDefault) {
	auto hRes = FindResourceExW(hInstance, lpType, lpName, wLanguage);
	if (!hRes) {
		if (!fallbackToDefault || wLanguage == MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL))
			throw Error("FindResourceExW");

		hRes = FindResourceExW(hInstance, lpType, lpName, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
		if (!hRes)
			throw Error("FindResourceExW(Fallback)");
	}
	Attach(LoadResource(hInstance, hRes), nullptr, true, "LoadResource");
}

void* Utils::Win32::GlobalResource::GetData() const {
	if (!m_object)
		throw std::runtime_error("Cannot get data of null resource");
	return LockResource(m_object);
}

Utils::Win32::Menu::Menu(HINSTANCE hInstance, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage, bool fallbackToDefault) {
	Attach(LoadMenuIndirectW(GlobalResource(hInstance, lpType, lpName, wLanguage, fallbackToDefault).GetData()), Null, true, "LoadMenuIndirectW");
}

void Utils::Win32::Menu::AttachAndSwap(HWND hWnd) {
	const auto hPrevMenu = GetMenu(hWnd);
	if (!SetMenu(hWnd, m_object))
		throw Error("SetMenu");
	m_object = hPrevMenu;
	m_bOwnership = !!hPrevMenu;
	DrawMenuBar(hWnd);
}

Utils::Win32::Accelerator::Accelerator(HINSTANCE hInstance, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage, bool fallbackToDefault) {
	union ACCELRES {
		ACCEL accel;
		uint64_t pad;
	};
	const auto hRes = GlobalResource(hInstance, lpType, lpName, wLanguage, fallbackToDefault);

	std::vector<ACCEL> accels;
	for (auto pAccel = static_cast<const ACCELRES*>(hRes.GetData()); ; pAccel++) {
		accels.emplace_back(pAccel->accel);
		if (pAccel->accel.fVirt & 0x80)
			break;
	}

	Attach(CreateAcceleratorTableW(&accels[0], static_cast<int>(accels.size())), Null, true, "CreateAcceleratorTable");
}

LPCWSTR Utils::Win32::FindStringResourceEx(HINSTANCE hInstance, UINT uId, WORD wLanguage, bool fallbackToDefault) {
	const auto hRes = GlobalResource(hInstance, RT_STRING, MAKEINTRESOURCE(uId / 16 + 1), wLanguage, fallbackToDefault);
	if (auto pwsz = static_cast<LPCWSTR>(hRes.GetData())) {
		for (UINT i = 0; i < (uId & 15); i++)
			pwsz = pwsz + 1 + static_cast<UINT>(*pwsz);
		return pwsz;
	}

	if (wLanguage != MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL))
		return FindStringResourceEx(hInstance, uId);

	throw std::runtime_error("FindStringResourceEx");
}

std::wstring Utils::Win32::MB_GetString(int i) {
	static LPCWSTR(WINAPI * MB_GetString)(int) = nullptr;
	if (!MB_GetString) {
		const auto pUser32 = LoadLibraryW(L"user32.dll");
		assert(pUser32);
		MB_GetString = reinterpret_cast<decltype(MB_GetString)>(GetProcAddress(pUser32, "MB_GetString"));
	}

	auto res = MB_GetString(i);
	std::wstring result;
	result.reserve(wcslen(res));
	while (*res) {
		if (*res != '&')
			result += *res;
		++res;
	}
	return result;
}
