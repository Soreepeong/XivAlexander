#pragma once

namespace Utils {
	namespace Win32 {
		class ActivationContext;
		class LoadedModule;
	}
}

namespace Dll {
	HWND FindGameMainWindow(bool throwOnError = true);

	const Utils::Win32::LoadedModule& Module();
	const Utils::Win32::ActivationContext& ActivationContext();
	size_t DisableUnloading(const char* pszReason);
	const char* GetUnloadDisabledReason();
	bool IsLoadedAsDependency();

	const wchar_t* GetGenericMessageBoxTitle();
	
	int MessageBoxF(HWND hWnd, UINT uType, const std::wstring& text);
	int MessageBoxF(HWND hWnd, UINT uType, const std::string& text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* text);
	int MessageBoxF(HWND hWnd, UINT uType, const char* text);

	LPCWSTR GetStringResFromId(UINT resId);
	
	int MessageBoxF(HWND hWnd, UINT uType, UINT stringResId);

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, UINT formatStringResId, Args ... args) {
		return MessageBoxF(hWnd, uType, std::format(GetStringResFromId(formatStringResId), std::forward<Args>(args)...).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* format, Args ... args) {
		return MessageBoxF(hWnd, uType, std::format(format, std::forward<Args>(args)...).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const char* format, Args ... args) {
		return MessageBoxF(hWnd, uType, FromUtf8(std::format(format, std::forward<Args>(args)...)).c_str());
	}

	std::wstring GetOriginalCommandLine();
	[[nodiscard]] bool IsOriginalCommandLineObfuscated();
	[[nodiscard]] bool IsLanguageRegionModifiable();

	void SetLoadedFromEntryPoint();
	[[nodiscard]] bool IsLoadedFromEntryPoint();
}
