#pragma once

#ifndef XIVALEXANDER_DLLEXPORT

#include <minwindef.h>
#include "Game/CommandLine.h"
#include "Utils/Win32/Process.h"

#ifdef XIVALEXANDER_DLLEXPORT_SET
#define XIVALEXANDER_DLLEXPORT __declspec(dllexport)
#else
#define XIVALEXANDER_DLLEXPORT
#endif

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

	const wchar_t* GetGenericMessageBoxTitle();

	int MessageBoxF(HWND hWnd, UINT uType, const std::wstring& text);
	int MessageBoxF(HWND hWnd, UINT uType, const std::string& text);
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* text);
	int MessageBoxF(HWND hWnd, UINT uType, const char* text);

	LPCWSTR GetStringResFromId(UINT resId);

	int MessageBoxF(HWND hWnd, UINT uType, UINT stringResId);

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, UINT formatStringResId, Args&& ... args) {
		return MessageBoxF(hWnd, uType, std::vformat(GetStringResFromId(formatStringResId), std::make_wformat_args(std::forward<Args&>(args)...)).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const wchar_t* format, Args&& ... args) {
		return MessageBoxF(hWnd, uType, std::vformat(format, std::make_wformat_args(std::forward<Args&>(args)...)).c_str());
	}

	template <typename ... Args>
	int MessageBoxF(HWND hWnd, UINT uType, const char* format, Args&& ... args) {
		return MessageBoxF(hWnd, uType, xivres::util::unicode::convert<std::wstring>(std::vformat(format, std::make_format_args(std::forward<Args&>(args)...))).c_str());
	}

	std::wstring GetOriginalCommandLine();
	// What the game read: the original with the remembered language and region applied, when XivAlexander started with it.
	std::wstring GetGameCommandLine();
	[[nodiscard]] bool IsOriginalCommandLineObfuscated();
	void ReplaceOriginalCommandLine(XivAlexander::Game::CommandLine::ArgumentList params);
	/// False also when the game's command line couldn't be read.
	[[nodiscard]] bool IsLanguageRegionModifiable();
	/// Why the game's command line couldn't be read, leaving what depends on it off; empty if it could.
	[[nodiscard]] const std::string& GetCommandLineError();

	void SetLoadedFromEntryPoint();
	[[nodiscard]] bool IsLoadedFromEntryPoint();

#ifdef XIVALEXANDER_LOADER
	/// XivAlexanderLoader64.exe: its folder, where the loader DLLs and XivAlexander64.dll are.
	std::filesystem::path ProgramDirectory();
#endif

	struct InjectEntryPointParameters;

	enum class LoaderAction : int {
		Interactive,
		Web,
		Ask,
		Load,
		Unload,
		Launcher,
		UpdateCheck,
		Install,
		Uninstall,
		Internal_Update_DependencyDllMode,
		Internal_Update_Step2_ReplaceFiles,
		Internal_Update_Step3_CleanupFiles,
		Internal_Inject_HookEntryPoint,
		Internal_Inject_LoadXivAlexanderImmediately,
		Internal_Inject_UnloadFromHandle,
		Count_,  // for internal use only
	};

	XIVALEXANDER_DLLEXPORT const char* LoaderActionToString(LoaderAction val);

	XIVALEXANDER_DLLEXPORT DWORD LaunchXivAlexLoaderWithTargetHandles(
		const std::vector<Utils::Win32::Process>& hSources,
		LoaderAction action,
		bool wait,
		const Utils::Win32::Process& waitForBeforeStarting = {},
		const std::filesystem::path& loaderPath = {});

	/// Makes the process load the loader DLL of its bitness when it reaches its entry point, which in the game then loads
	/// XivAlexander64.dll. Returns the parameters' address in that process. XivAlexanderLoader64.exe calls
	/// XA_PatchEntryPoint of XivAlexanderLoader64.dll for it.
	XIVALEXANDER_DLLEXPORT InjectEntryPointParameters* PatchEntryPointForInjection(HANDLE hProcess);

	//
	// Everything declared below must be able to be called from CreateRemoteProcess.
	//

	class InjectOnCreateProcessAppFlags {
	public:
		enum : size_t {
			Use = 1 << 0,
			InjectAll = 1 << 1,
			InjectGameOnly = 1 << 2,
		};
	};

	extern "C" XIVALEXANDER_DLLEXPORT size_t EnableInjectOnCreateProcess(size_t flags);

	struct InjectEntryPointParameters {
		void* EntryPoint;
		void* EntryPointOriginalBytes;
		size_t EntryPointOriginalLength;
		void* TrampolineAddress;
		bool LoadedAsProxy;  // set by a loader DLL in the game folder on its own process
	};

	extern "C" XIVALEXANDER_DLLEXPORT void InjectEntryPoint(InjectEntryPointParameters* pParam);
	// In XivAlexanderLoader64.dll, loads XivAlexander64.dll and turns it on, or turns it off and lets go of it.
	extern "C" XIVALEXANDER_DLLEXPORT size_t EnableXivAlexander(size_t bEnable);
	extern "C" XIVALEXANDER_DLLEXPORT size_t ReloadConfiguration(void* lpReserved);
	extern "C" XIVALEXANDER_DLLEXPORT size_t DisableAllApps(void* lpReserved);
	extern "C" XIVALEXANDER_DLLEXPORT void CallFreeLibrary(void*);

	enum class CheckPackageVersionResult {
		OK = 0,
		MissingFiles = 1,
		VersionMismatch = 2,
	};

	XIVALEXANDER_DLLEXPORT [[nodiscard]] CheckPackageVersionResult CheckPackageVersion();

	// XivAlexanderLoader64.exe: parses the command line and runs the requested action.
	int LoaderAppMain();

	struct VersionInformation {
		std::string Name;
		std::string Body;
		std::chrono::zoned_time<std::chrono::seconds> PublishDate;
		std::string DownloadLink;
		size_t DownloadSize{};
	};

	VersionInformation CheckUpdates();

	bool IsXivAlexanderDll(const std::filesystem::path& dllPath);
	
	const wchar_t GameExecutable64NameW[] = L"ffxiv_dx11.exe";
	const wchar_t GameExecutableNameW[] = L"ffxiv_dx11.exe";
	const wchar_t XivAlexLoader64NameW[] = L"XivAlexanderLoader64.exe";
	const wchar_t XivAlexLoaderDll32NameW[] = L"XivAlexanderLoader32.dll";
	const wchar_t XivAlexLoaderDll64NameW[] = L"XivAlexanderLoader64.dll";
	const wchar_t XivAlexDll64NameW[] = L"XivAlexander64.dll";

	// The 64-bit loader is the only one; it patches 32-bit processes itself.
	const wchar_t XivAlexLoaderNameW[] = L"XivAlexanderLoader64.exe";

#if INTPTR_MAX != INT64_MAX && INTPTR_MAX != INT32_MAX
#error "Environment not x86 or x64."
#endif
}

#endif
