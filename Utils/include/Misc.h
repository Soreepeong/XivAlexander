#pragma once

#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

namespace Utils {
	std::wstring FromUtf8(const std::string&);
	std::string ToUtf8(const std::wstring&);
	
	SYSTEMTIME EpochToLocalSystemTime(uint64_t epochMilliseconds);
	uint64_t GetHighPerformanceCounter(int32_t multiplier = 1000);
	
	int sockaddr_cmp(const void* x, const void* y);
	std::string DescribeSockaddr(const struct sockaddr_in& sa);
	std::string DescribeSockaddr(const struct sockaddr_in6& sa);
	std::string DescribeSockaddr(const struct sockaddr& sa);
	std::string DescribeSockaddr(const struct sockaddr_storage& sa);

	[[nodiscard]]
	std::string FormatString(const _Printf_format_string_ char* format, ...);

	[[nodiscard]]
	std::wstring FormatString(const _Printf_format_string_ wchar_t* format, ...);

	[[nodiscard]]
	std::vector<std::string> StringSplit(const std::string& str, const std::string& delimiter);

	[[nodiscard]]
	std::string StringTrim(const std::string& str, bool leftTrim = true, bool rightTrim = true);

	void SetThreadDescription(HANDLE hThread, const std::wstring& description);

	template <typename ... Args>
	void SetThreadDescription(HANDLE hThread, const _Printf_format_string_ wchar_t* format, Args ... args) {
		SetThreadDescription(hThread, FormatString(format, std::forward<Args>(args)...));
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const _Printf_format_string_ wchar_t* format, Args ... args) {
		return MessageBoxW(hWnd, Utils::FormatString(format, std::forward<Args>(args)...).c_str(), lpCaption, uType);
	}

	void SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked);
	void SetMenuState(HWND hWnd, DWORD nMenuId, bool bChecked);

	std::filesystem::path PathFromModule(HMODULE hModule = nullptr, HANDLE hProcess = INVALID_HANDLE_VALUE);

	[[nodiscard]]
	std::tuple<std::wstring, std::wstring> ResolveGameReleaseRegion();
	[[nodiscard]]
	std::tuple<std::wstring, std::wstring> ResolveGameReleaseRegion(const std::filesystem::path& path);

	class WindowsError : public std::runtime_error {
		const int m_nErrorCode;
	public:
		WindowsError(int errorCode, const std::string& msg);
		WindowsError(const std::string& msg);

		template<typename ... Args>
		WindowsError(const _Printf_format_string_ char* format, Args...args)
			: WindowsError(GetLastError(), FormatString(format, std::forward<Args>(args)...)) {
		}
		template<typename ... Args>
		WindowsError(int errorCode, const _Printf_format_string_ char* format, Args...args)
			: WindowsError(errorCode, FormatString(format, std::forward<Args>(args)...)) {
		}
	};
}
