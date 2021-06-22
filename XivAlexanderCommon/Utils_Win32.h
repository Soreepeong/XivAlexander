#pragma once

#include <windef.h>
#include "Utils__String.h"

namespace Utils::Win32 {

	void SetThreadDescription(HANDLE hThread, const std::wstring& description);

	template <typename ... Args>
	void SetThreadDescription(HANDLE hThread, const _Printf_format_string_ wchar_t* format, Args ... args) {
		SetThreadDescription(hThread, FormatString(format, std::forward<Args>(args)...));
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const _Printf_format_string_ wchar_t* format, Args ... args) {
		return MessageBoxW(hWnd, FormatString(format, std::forward<Args>(args)...).c_str(), lpCaption, uType);
	}

	void SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked);
	void SetMenuState(HWND hWnd, DWORD nMenuId, bool bChecked);

	std::string FormatWindowsErrorMessage(unsigned int errorCode);

	std::pair<std::string, std::string> FormatModuleVersionString(HMODULE hModule);

	BOOL EnableTokenPrivilege(HANDLE hToken, LPCTSTR Privilege, BOOL bEnablePrivilege);

	void AddDebugPrivilege();
	
	class Error : public std::runtime_error {
		const int m_nErrorCode;

	public:
		Error(int errorCode, const std::string& msg);
		explicit Error(const std::string& msg);

		template<typename ... Args>
		explicit Error(const _Printf_format_string_ char* format, Args...args)
			: Error(FormatString(format, std::forward<Args>(args)...)) {
		}
		template<typename ... Args>
		Error(int errorCode, const _Printf_format_string_ char* format, Args...args)
			: Error(errorCode, FormatString(format, std::forward<Args>(args)...)) {
		}
	};
}
