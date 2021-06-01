#pragma once

#include <string>
#include <algorithm>
#include <vector>

namespace Utils {
	std::wstring FromOem(const std::string&);
	std::wstring FromUtf8(const std::string&);
	std::string ToUtf8(const std::string&);
	std::string ToUtf8(const std::wstring&);

	uint64_t GetEpoch();
	SYSTEMTIME EpochToLocalSystemTime(uint64_t epochMilliseconds);
	uint64_t GetHighPerformanceCounter(int32_t multiplier = 1000);

	std::string FormatWindowsErrorMessage(unsigned int errorCode = -1);
	
	int sockaddr_cmp(const void* x, const void* y);

	template <typename ... Args>
	std::string FormatString(const _Printf_format_string_ char* format, Args ... args) {
		std::string buf;
		buf.resize(512);
		do {
			const auto length = _snprintf_s(&buf[0], buf.capacity(), _TRUNCATE, format, args ...);
			if (length >= 0) {
				buf.resize(length);
				return buf;
			}
			buf.resize(std::min(buf.capacity() * 2, buf.capacity() + 1024));
		} while (true);
	}

	template <typename ... Args>
	std::string FormatString(const _Printf_format_string_ std::string& format, Args ... args) {
		return FormatString(format.c_str(), std::forward<Args>(args)...);
	}

	template <typename ... Args>
	std::wstring FormatString(const _Printf_format_string_ wchar_t* format, Args ... args) {
		std::wstring buf;
		buf.resize(512);
		do {
			const auto length = _snwprintf_s(&buf[0], buf.capacity(), _TRUNCATE, format, args ...);
			if (length >= 0) {
				buf.resize(length);
				return buf;
			}
			buf.resize(std::min(buf.capacity() * 2, buf.capacity() + 1024));
		} while (true);
	}

	template <typename ... Args>
	std::wstring FormatString(const _Printf_format_string_ std::wstring& format, Args ... args) {
		return FormatString(format.c_str(), std::forward<Args>(args)...);
	}

	std::vector<std::string> StringSplit(const std::string& str, const std::string& delimiter);
	std::string StringTrim(const std::string& str, bool leftTrim = true, bool rightTrim = true);

	template <typename ... Args>
	void SetThreadDescription(HANDLE hThread, const _Printf_format_string_ std::wstring& format, Args ... args) {
		typedef HRESULT(WINAPI* SetThreadDescriptionT)(
			_In_ HANDLE hThread,
			_In_ PCWSTR lpThreadDescription
			);
		SetThreadDescriptionT pfnSetThreadDescription = nullptr;

		if (const auto hMod = GetModuleHandleW(L"kernel32.dll"))
			pfnSetThreadDescription = reinterpret_cast<SetThreadDescriptionT>(GetProcAddress(hMod, "SetThreadDescription"));
		else if (const auto hMod = GetModuleHandleW(L"KernelBase.dll"))
			pfnSetThreadDescription = reinterpret_cast<SetThreadDescriptionT>(GetProcAddress(hMod, "SetThreadDescription"));

		if (pfnSetThreadDescription)
			pfnSetThreadDescription(GetCurrentThread(), FormatString(format, std::forward<Args>(args)...).c_str());
	}

	void SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked);
	void SetMenuState(HWND hWnd, DWORD nMenuId, bool bChecked);
}