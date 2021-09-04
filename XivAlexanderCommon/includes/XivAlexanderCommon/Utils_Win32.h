#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <windef.h>
#include <shtypes.h>

namespace Utils::Win32 {

	void SetThreadDescription(HANDLE hThread, const std::wstring& description);

	template <typename ... Args>
	void SetThreadDescription(HANDLE hThread, const wchar_t* format, Args ... args) {
		SetThreadDescription(hThread, std::format(format, std::forward<Args>(args)...));
	}

	template <typename ... Args>
	void DebugPrint(const wchar_t* format, Args ... args) {
		OutputDebugStringW(std::format(L"{}\n", std::format(format, std::forward<Args>(args)...)).c_str());
	}

	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const std::wstring& text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const std::string& text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const wchar_t* text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const char* text);

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const wchar_t* format, Args ... args) {
		return MessageBoxF(hWnd, uType, lpCaption, std::format(format, std::forward<Args>(args)...).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const char* format, Args ... args) {
		return MessageBoxF(hWnd, uType, lpCaption, FromUtf8(std::format(format, std::forward<Args>(args)...)).c_str());
	}

	void SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked, bool bEnabled);

	std::string FormatWindowsErrorMessage(unsigned int errorCode);

	std::pair<std::string, std::string> FormatModuleVersionString(const void* pBlock);
	std::pair<std::string, std::string> FormatModuleVersionString(const std::filesystem::path& path);
	std::pair<std::string, std::string> FormatModuleVersionString(HMODULE hModule);

	bool EnableTokenPrivilege(HANDLE hToken, LPCTSTR Privilege, bool bEnablePrivilege);
	void AddDebugPrivilege();
	bool IsUserAnAdmin();

	std::filesystem::path GetMappedImageNativePath(HANDLE hProcess, void* lpMem);
	std::filesystem::path ToNativePath(const std::filesystem::path& path);
	std::filesystem::path GetSystem32Path();
	std::filesystem::path EnsureKnownFolderPath(_In_ REFKNOWNFOLDERID rfid);

	struct RunProgramParams {
		enum ElevateMode {
			Normal,
			Force,
			NeverUnlessAlreadyElevated,
			NeverUnlessShellIsElevated,
			CancelIfRequired,
			NoElevationIfDenied,
		};

		std::filesystem::path path;
		std::filesystem::path dir;
		std::wstring args;
		bool wait = false;
		ElevateMode elevateMode = Normal;
		bool throwOnCancel = false;
	};
	bool RunProgram(RunProgramParams params);

	std::wstring GetCommandLineWithoutProgramName();
	std::wstring ReverseCommandLineToArgv(const std::wstring& argv);
	std::wstring ReverseCommandLineToArgv(const std::span<const std::wstring>& argv);
	std::wstring ReverseCommandLineToArgv(const std::initializer_list<const std::wstring>& argv);
	std::string ReverseCommandLineToArgv(const std::string& argv);
	std::string ReverseCommandLineToArgv(const std::span<const std::string>& argv);
	std::string ReverseCommandLineToArgv(const std::initializer_list<const std::string>& argv);

	std::vector<DWORD> GetProcessList();

	extern HANDLE g_hDefaultHeap;

	class Error : public std::runtime_error {
		const DWORD m_nErrorCode;

	public:
		Error(DWORD errorCode, const std::string& msg);
		explicit Error(const std::string& msg);

		template<typename Arg, typename ... Args>
		Error(const char* format, Arg arg1, Args...args)
			: Error(std::format(format, std::move(arg1), std::forward<Args>(args)...)) {
		}
		template<typename Arg, typename ... Args>
		Error(DWORD errorCode, const char* format, Arg arg1, Args...args)
			: Error(errorCode, std::format(format, std::move(arg1), std::forward<Args>(args)...)) {
		}

		[[nodiscard]] auto Code() const { return m_nErrorCode; }
	};
}
