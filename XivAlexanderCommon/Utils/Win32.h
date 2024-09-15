#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <windef.h>
#include <shtypes.h>

namespace Utils::Win32 {
	
	std::wstring TryGetThreadDescription(HANDLE hThread);
	void SetThreadDescription(HANDLE hThread, const std::wstring& description);

	template <typename ... Args>
	void SetThreadDescription(HANDLE hThread, const wchar_t* format, Args&& ... args) {
		SetThreadDescription(hThread, std::vformat(format, std::make_wformat_args(std::forward<Args&>(args)...)));
	}

	template <typename ... Args>
	void DebugPrint(const wchar_t* format, Args&& ... args) {
		OutputDebugStringW(std::format(L"{}\n", std::vformat(format, std::make_wformat_args(std::forward<Args&>(args)...))).c_str());
	}

	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const std::wstring& text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const std::string& text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const wchar_t* text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const char* text);

	std::filesystem::path TranslatePath(const std::filesystem::path& path, const std::filesystem::path& relativeTo);
	std::filesystem::path EnsureDirectory(const std::filesystem::path& path);
	std::filesystem::path ResolvePathFromFileName(const std::filesystem::path& path, const std::filesystem::path& ext = {});

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const wchar_t* format, Args&& ... args) {
		return MessageBoxF(hWnd, uType, lpCaption, std::vformat(format, std::make_wformat_args(std::forward<Args&>(args)...)).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* lpCaption, const char* format, Args&& ... args) {
		return MessageBoxF(hWnd, uType, lpCaption, FromUtf8(std::vformat(format, std::make_format_args(std::forward<Args&>(args)...))).c_str());
	}

	void SetMenuState(HMENU hMenu, DWORD nMenuId, bool bChecked, bool bEnabled, std::wstring newText = {});

	std::string FormatWindowsErrorMessage(unsigned int errorCode, int languageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT));

	std::pair<std::string, std::string> FormatModuleVersionString(const void* pBlock);
	std::pair<std::string, std::string> FormatModuleVersionString(const std::filesystem::path& path);
	std::pair<std::string, std::string> FormatModuleVersionString(HMODULE hModule);

	bool EnableTokenPrivilege(HANDLE hToken, LPCTSTR Privilege, bool bEnablePrivilege);
	void AddDebugPrivilege();
	bool IsUserAnAdmin();

	inline uint32_t GetCoreCount() noexcept {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		return std::max<uint32_t>(1, si.dwNumberOfProcessors);
	}

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

	void ShellExecutePathOrThrow(const std::filesystem::path& path, HWND hwndOwner = nullptr);

	// Needs to be null-terminated, and wstring_view does not guarantee that
	std::vector<std::wstring> CommandLineToArgs(const std::wstring& = {});
	std::vector<std::string> CommandLineToArgsU8(const std::wstring& = {});
	std::pair<std::wstring, std::wstring> SplitCommandLineIntoNameAndArgs(std::wstring = {});
	std::wstring ReverseCommandLineToArgv(const std::wstring& argv);
	std::wstring ReverseCommandLineToArgv(const std::span<const std::wstring>& argv);
	std::wstring ReverseCommandLineToArgv(const std::initializer_list<const std::wstring>& argv);
	std::string ReverseCommandLineToArgv(const std::string& argv);
	std::string ReverseCommandLineToArgv(const std::span<const std::string>& argv);
	std::string ReverseCommandLineToArgv(const std::initializer_list<const std::string>& argv);

	std::vector<DWORD> GetProcessList();

	extern HANDLE g_hDefaultHeap;

	class Error : public std::runtime_error {
		static int DefaultLanguageId;

		const DWORD m_nErrorCode;

	public:
		Error(DWORD errorCode, const std::string& msg);
		explicit Error(const std::string& msg);

		Error(const _com_error& e);

		template<typename Arg, typename ... Args>
		Error(const char* format, Arg&& arg1, Args&&...args)
			: Error(std::vformat(format, std::make_format_args(std::forward<Arg&>(arg1), std::forward<Args&>(args)...))) {
		}
		template<typename Arg, typename ... Args>
		Error(DWORD errorCode, const char* format, Arg&& arg1, Args&&...args)
			: Error(errorCode, std::vformat(format, std::make_format_args(std::forward<Arg&>(arg1), std::forward<Args&>(args)...))) {
		}

		[[nodiscard]] auto Code() const { return m_nErrorCode; }

		static void SetDefaultLanguageId(int languageId) {
			DefaultLanguageId = languageId;
		}

		static void ThrowIfFailed(HRESULT hresult, bool expectCancel = false);
	};
	
	class CancelledError : public Error {
	public:
		using Error::Error;
		CancelledError() : Error(ERROR_CANCELLED, "Cancelled") {}
	};

}
