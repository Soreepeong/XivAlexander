#pragma once

#include <windef.h>

namespace Utils::Win32 {

	void SetThreadDescription(HANDLE hThread, const std::wstring& description);

	template <typename ... Args>
	void SetThreadDescription(HANDLE hThread, const wchar_t* format, Args ... args) {
		SetThreadDescription(hThread, std::format(format, std::forward<Args>(args)...));
	}

	template <typename ... Args>
	void DebugPrint(const wchar_t* format, Args ... args) {
		OutputDebugStringW(std::format(format, std::forward<Args>(args)...).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const wchar_t* format, Args ... args) {
		return MessageBoxW(hWnd, std::format(format, std::forward<Args>(args)...).c_str(), lpCaption, uType);
	}

	void SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked);
	void SetMenuState(HWND hWnd, DWORD nMenuId, bool bChecked);

	std::string FormatWindowsErrorMessage(unsigned int errorCode);

	std::pair<std::string, std::string> FormatModuleVersionString(HMODULE hModule);

	bool EnableTokenPrivilege(HANDLE hToken, LPCTSTR Privilege, bool bEnablePrivilege);
	void AddDebugPrivilege();
	bool IsUserAnAdmin();

	std::filesystem::path GetMappedImageNativePath(HANDLE hProcess, void* lpMem);
	std::filesystem::path ToNativePath(const std::filesystem::path& path);

	std::vector<DWORD> GetProcessList();
	
	class Error : public std::runtime_error {
		const int m_nErrorCode;

	public:
		Error(int errorCode, const std::string& msg);
		explicit Error(const std::string& msg);

		template<typename ... Args>
		Error(const char* format, Args...args)
			: Error(std::format(format, std::forward<Args>(args)...)) {
		}
		template<typename ... Args>
		Error(int errorCode, const char* format, Args...args)
			: Error(errorCode, std::format(format, std::forward<Args>(args)...)) {
		}

		int Code() const;
	};
}
