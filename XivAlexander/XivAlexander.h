#pragma once

#ifndef XIVALEXANDER_DLLEXPORT

#include <minwindef.h>
#include <XivAlexanderCommon/Utils/Win32/Process.h>

#ifdef XIVALEXANDER_DLLEXPORT_SET
#define XIVALEXANDER_DLLEXPORT __declspec(dllexport)
#else
#define XIVALEXANDER_DLLEXPORT __declspec(dllimport)
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

	enum WhichLoader {
		Current,
		Opposite,
		Force32,
		Force64,
	};
	XIVALEXANDER_DLLEXPORT DWORD LaunchXivAlexLoaderWithTargetHandles(
		const std::vector<Utils::Win32::Process>& hSources,
		LoaderAction action,
		bool wait,
		const Utils::Win32::Process& waitForBeforeStarting = {},
		WhichLoader which = Current,
		const std::filesystem::path& loaderPath = {});
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

	extern "C" XIVALEXANDER_DLLEXPORT size_t __stdcall EnableInjectOnCreateProcess(size_t flags);

	struct InjectEntryPointParameters {
		void* EntryPoint;
		void* EntryPointOriginalBytes;
		size_t EntryPointOriginalLength;
		void* TrampolineAddress;
		bool SkipFree;
		bool LoadInstalledXivAlexDllOnly;

		struct {
			HANDLE hWorkerThread;
			HANDLE hMainThread;
		} Internal;
	};

	extern "C" XIVALEXANDER_DLLEXPORT void __stdcall InjectEntryPoint(InjectEntryPointParameters* pParam);
	extern "C" XIVALEXANDER_DLLEXPORT size_t __stdcall EnableXivAlexander(size_t bEnable);
	extern "C" XIVALEXANDER_DLLEXPORT size_t __stdcall ReloadConfiguration(void* lpReserved);
	extern "C" XIVALEXANDER_DLLEXPORT size_t __stdcall DisableAllApps(void* lpReserved);
	extern "C" XIVALEXANDER_DLLEXPORT void __stdcall CallFreeLibrary(void*);

	enum class CheckPackageVersionResult {
		OK = 0,
		MissingFiles = 1,
		VersionMismatch = 2,
	};

	XIVALEXANDER_DLLEXPORT [[nodiscard]] CheckPackageVersionResult CheckPackageVersion();

	extern "C" int XA_LoaderApp();

	struct VersionInformation {
		std::string Name;
		std::string Body;
		std::chrono::zoned_time<std::chrono::seconds> PublishDate;
		std::string DownloadLink;
		size_t DownloadSize{};
	};

	VersionInformation CheckUpdates();

	bool IsXivAlexanderDll(const std::filesystem::path& dllPath);
	
	const wchar_t GameExecutable32NameW[] = L"ffxiv.exe";
	const wchar_t GameExecutable64NameW[] = L"ffxiv_dx11.exe";
	const wchar_t XivAlexLoader32NameW[] = L"XivAlexanderLoader32.exe";
	const wchar_t XivAlexLoader64NameW[] = L"XivAlexanderLoader64.exe";
	const wchar_t XivAlexDll32NameW[] = L"XivAlexander32.dll";
	const wchar_t XivAlexDll64NameW[] = L"XivAlexander64.dll";

#if INTPTR_MAX == INT32_MAX

	const wchar_t GameExecutableNameW[] = L"ffxiv.exe";
	const wchar_t XivAlexDllNameW[] = L"XivAlexander32.dll";
	const char XivAlexDllName[] = "XivAlexander32.dll";
	const wchar_t XivAlexLoaderNameW[] = L"XivAlexanderLoader32.exe";
	const wchar_t GameExecutableOppositeNameW[] = L"ffxiv_dx11.exe";
	const wchar_t XivAlexDllOppositeNameW[] = L"XivAlexander64.dll";
	const char XivAlexDllOppositeName[] = "XivAlexander64.dll";
	const wchar_t XivAlexLoaderOppositeNameW[] = L"XivAlexanderLoader64.exe";

#elif INTPTR_MAX == INT64_MAX

	const wchar_t GameExecutableNameW[] = L"ffxiv_dx11.exe";
	const wchar_t XivAlexDllNameW[] = L"XivAlexander64.dll";
	const char XivAlexDllName[] = "XivAlexander64.dll";
	const wchar_t XivAlexLoaderNameW[] = L"XivAlexanderLoader64.exe";
	const wchar_t GameExecutableOppositeNameW[] = L"ffxiv.exe";
	const wchar_t XivAlexDllOppositeNameW[] = L"XivAlexander32.dll";
	const char XivAlexDllOppositeName[] = "XivAlexander32.dll";
	const wchar_t XivAlexLoaderOppositeNameW[] = L"XivAlexanderLoader32.exe";

#else
#error "Environment not x86 or x64."
#endif
}

#endif
