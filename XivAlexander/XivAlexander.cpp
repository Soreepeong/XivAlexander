#include "pch.h"

#include <XivAlexander/XivAlexander.h>
#include "Game/CommandLine.h"
#include "Utils/Win32/Resource.h"

#include "Config.h"
#include "LoaderInterface.h"
#include "MainApp/Features/LoginSessions.h"
#include "Misc/CrashMessageBoxHandler.h"
#include "Misc/GameInstallationDetector.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "resource.h"
#include "XivAlexander.h"

namespace Game = XivAlexander::Game;
namespace LoaderInterface = XivAlexander::LoaderInterface;

namespace {
	Utils::Win32::LoadedModule s_hModule;
	Utils::Win32::ActivationContext s_hActivationContext;
	std::string s_dllUnloadDisableReason;
	std::unique_ptr<XivAlexander::Misc::CrashMessageBoxHandler> s_crashMessageBoxHandler;

	std::mutex s_initializeMtx;
	bool s_initialized = false;
	LoaderInterface::LoaderInfo s_loaderInfo{};
	std::wstring s_loaderPath;
	std::wstring s_programDirectory;

	std::wstring s_originalCommandLine;
	bool s_originalCommandLineIsObfuscated;

	std::wstring s_gameCommandLineW;
	std::string s_gameCommandLineA;

	[[nodiscard]] bool IsGameProcess() {
		return lstrcmpiW(Utils::Win32::Process::Current().PathOf().filename().wstring().c_str(), Dll::GameExecutableNameW) == 0;
	}

	void SetGameCommandLine(XivAlexander::Game::CommandLine::ArgumentList params) {
		if (Dll::IsLanguageRegionModifiable()) {
			auto config = XivAlexander::Config::Acquire();
			Game::CommandLine::WellKnown::SetLanguage(params, config->Runtime.Launch.RememberedLanguage);
			Game::CommandLine::WellKnown::SetRegion(params, config->Runtime.Launch.RememberedRegion);
			OutputDebugStringW(std::format(L"Parameters modified (language={} region={})\n",
				static_cast<int>(config->Runtime.Launch.RememberedLanguage.Value()),
				static_cast<int>(config->Runtime.Launch.RememberedRegion.Value())).c_str());
		}

		s_gameCommandLineW = std::format(L"\"{}\" {}", Utils::Win32::Process::Current().PathOf().wstring(), Game::CommandLine::ToString(params, false));
		s_gameCommandLineA = Utils::ToAnsi(s_gameCommandLineW);
	}

	// The game starting itself again passes on the decoded GetCommandLineW; it gets the arguments obfuscated as they came.
	const wchar_t* __stdcall FilterCreateProcessArguments(const wchar_t* applicationPath, const wchar_t* arguments) {
		thread_local std::wstring s_arguments;
		try {
			if (!s_originalCommandLineIsObfuscated || !applicationPath || !arguments)
				return nullptr;
			const auto path = std::filesystem::path(applicationPath);
			std::error_code ec;
			if (lstrcmpiW(path.filename().c_str(), Dll::GameExecutableNameW) != 0 || !equivalent(path, Utils::Win32::Process::Current().PathOf(), ec))
				return nullptr;
			s_arguments = Game::CommandLine::ToString(Game::CommandLine::FromString(std::format(L"unused.exe {}", arguments)), true);
			return s_arguments.c_str();
		} catch (...) {
			return nullptr;
		}
	}

	void CheckObfuscatedArguments() {
		if (!IsGameProcess())
			return;

		try {
			SetGameCommandLine(Game::CommandLine::FromString(Dll::GetOriginalCommandLine(), &s_originalCommandLineIsObfuscated));

			static XivAlexander::Misc::Hooks::ImportedFunction<LPWSTR> GetCommandLineW("kernel32!GetCommandLineW", "kernel32.dll", "GetCommandLineW");
			static const auto h1 = GetCommandLineW.SetHook([]() -> LPWSTR {
				return s_gameCommandLineW.data();
			});

			static XivAlexander::Misc::Hooks::ImportedFunction<LPSTR> GetCommandLineA("kernel32!GetCommandLineA", "kernel32.dll", "GetCommandLineA");
			static const auto h2 = GetCommandLineA.SetHook([]() -> LPSTR {
				return s_gameCommandLineA.data();
			});

			if (XA_LOADERINFO_HAS(&s_loaderInfo, SetCreateProcessArgumentFilter) && s_loaderInfo.SetCreateProcessArgumentFilter)
				s_loaderInfo.SetCreateProcessArgumentFilter(&FilterCreateProcessArguments);
		} catch (const std::exception& e) {
			OutputDebugStringW(std::format(L"Error in CheckObfuscatedArguments: {}\n", e.what()).c_str());
		}
	}

	// What has to be in place before the game's own initialization runs.
	void InitializeGameBeforeEntryPoint() {
		static xivres::util::on_dtor::multi s_hooks;

		static XivAlexander::Misc::Hooks::ImportedFunction<HANDLE, DWORD, BOOL, LPCSTR> s_OpenMutexA("kernel32!OpenMutexA", "kernel32.dll", "OpenMutexA");
		static XivAlexander::Misc::Hooks::ImportedFunction<HANDLE, LPSECURITY_ATTRIBUTES, BOOL, LPCSTR> s_CreateMutexA("kernel32!CreateMutexA", "kernel32.dll", "CreateMutexA");
		s_hooks += s_OpenMutexA.SetHook([](DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName) {
			if (lpName && std::string_view(lpName).starts_with("Global\\6AA83AB5-BAC4-4a36-9F66-A309770760CB_ffxiv_"))
				return static_cast<HANDLE>(0);

			return s_OpenMutexA.bridge(dwDesiredAccess, bInheritHandle, lpName);
			});
		s_hooks += s_CreateMutexA.SetHook([](LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) {
			if (lpName && std::string_view(lpName).starts_with("Global\\6AA83AB5-BAC4-4a36-9F66-A309770760CB_ffxiv_"))
				return s_CreateMutexA.bridge(lpMutexAttributes, bInitialOwner, nullptr);

			return s_CreateMutexA.bridge(lpMutexAttributes, bInitialOwner, lpName);
			});

		// Prevent the game from restarting to "fix" ACL
		static XivAlexander::Misc::Hooks::ImportedFunction<HANDLE, DWORD, BOOL, DWORD> s_OpenProcessForXiv("kernel32!OpenProcess", "kernel32.dll", "OpenProcess");
		static XivAlexander::Misc::Hooks::PointerFunction<HANDLE, DWORD, BOOL, DWORD> s_OpenProcess("OpenProcess", OpenProcess);
		if (s_OpenProcessForXiv) {
			s_hooks += s_OpenProcessForXiv.SetHook([](DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
				if (dwProcessId == GetCurrentProcessId()) {
					if (dwDesiredAccess & PROCESS_VM_WRITE) {
						SetLastError(ERROR_ACCESS_DENIED);
						return HANDLE{};
					}
				}
				return s_OpenProcess.bridge(dwDesiredAccess, bInheritHandle, dwProcessId);
				});
		}
		if (s_OpenProcess) {
			s_hooks += s_OpenProcess.SetHook([](DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
				if (dwProcessId == GetCurrentProcessId()) {
					// Prevent Reloaded from tripping
					if (HANDLE h{}; DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, dwDesiredAccess, bInheritHandle, 0))
						return h;
					return HANDLE{};
				}
				return s_OpenProcess.bridge(dwDesiredAccess, bInheritHandle, dwProcessId);
				});
		}

		// Started without a command line, as in double-clicking the game; use the session the launcher stored last.
		try {
			if (Game::CommandLine::FromString(Dll::GetOriginalCommandLine()).empty()) {
				if (auto stored = XivAlexander::Apps::MainApp::Features::LoginSessions::GetMostRecentlyStoredArguments()) {
					XivAlexander::Misc::Logger::Acquire()->Format(XivAlexander::LogCategory::General, "Started without arguments; using login session \"{}\"", stored->first);
					// The session keeps the language and region it was stored with; the remembered ones apply as on restarting.
					if (Dll::IsLanguageRegionModifiable()) {
						const auto& launch = XivAlexander::Config::Acquire()->Runtime.Launch;
						if (launch.RememberedLanguage != xivres::game_language::Unspecified)
							Game::CommandLine::WellKnown::SetLanguage(stored->second, launch.RememberedLanguage);
						if (launch.RememberedRegion != xivres::game_publisher::Unspecified)
							Game::CommandLine::WellKnown::SetRegion(stored->second, launch.RememberedRegion);
					}
					Dll::ReplaceOriginalCommandLine(std::move(stored->second));
				}
			}
		} catch (const std::exception& e) {
			XivAlexander::Misc::Logger::Acquire()->Format<XivAlexander::LogLevel::Warning>(XivAlexander::LogCategory::General, "Cannot use a stored login session: {}", e.what());
		}

		if (XivAlexander::Config::Acquire()->Runtime.FramerateControl.UseMoreCpuTime) {
			static XivAlexander::Misc::Hooks::ImportedFunction<DWORD_PTR, HANDLE, DWORD_PTR> s_SetThreadAffinityMask("kernel32!SetThreadAffinityMask", "kernel32.dll", "SetThreadAffinityMask");
			static XivAlexander::Misc::Hooks::ImportedFunction<void, LPSYSTEM_INFO> s_GetSystemInfo("kernel32!GetSystemInfo", "kernel32.dll", "GetSystemInfo");
			if (s_SetThreadAffinityMask) {
				s_hooks += s_SetThreadAffinityMask.SetHook([](HANDLE h, DWORD_PTR d) { return static_cast<DWORD_PTR>(-1); });
			}
			if (s_GetSystemInfo) {
				s_hooks += s_GetSystemInfo.SetHook([&](LPSYSTEM_INFO i) {
					s_GetSystemInfo.bridge(i);
					i->dwNumberOfProcessors = std::min(192UL, i->dwNumberOfProcessors * 8);
					});
			}
		}
	}

	// Copies what the loader gave, no further than either side knows.
	void StoreLoaderInfo(const LoaderInterface::LoaderInfo& info) {
		s_loaderInfo = {};
		memcpy(&s_loaderInfo, &info, (std::min<size_t>)(info.Size, sizeof s_loaderInfo));
		s_loaderInfo.Size = static_cast<uint32_t>((std::min<size_t>)(info.Size, sizeof s_loaderInfo));

		s_loaderPath = s_loaderInfo.LoaderPath ? s_loaderInfo.LoaderPath : L"";
		s_programDirectory = s_loaderInfo.ProgramDirectory ? s_loaderInfo.ProgramDirectory : L"";
		s_loaderInfo.LoaderPath = s_loaderPath.c_str();
		s_loaderInfo.ProgramDirectory = s_programDirectory.c_str();
		if (s_loaderInfo.OriginalCommandLine)
			s_originalCommandLine = s_loaderInfo.OriginalCommandLine;
		s_loaderInfo.OriginalCommandLine = nullptr;
	}
}

const Utils::Win32::LoadedModule& Dll::Module() {
	return s_hModule;
}

const Utils::Win32::ActivationContext& Dll::ActivationContext() {
	return s_hActivationContext;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH: {
			// The loader passes the command line it saw first; this is for when it is older and does not.
			s_originalCommandLine = GetCommandLineW();

			try {
				s_hModule.Attach(hInstance, Utils::Win32::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
				s_hActivationContext = Utils::Win32::ActivationContext(ACTCTXW{
					.cbSize = static_cast<ULONG>(sizeof ACTCTXW),
					.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
					.lpResourceName = MAKEINTRESOURCE(IDR_RT_MANIFEST_LATE_ACTIVATION),
					.hModule = Dll::Module(),
				});

				MH_Initialize();

			} catch (const std::exception& e) {
				Utils::Win32::DebugPrint(L"DllMain({:x}, DLL_PROCESS_ATTACH, {}) Error: {}",
					reinterpret_cast<size_t>(hInstance), reinterpret_cast<size_t>(lpReserved), e.what());
				return FALSE;
			}

			return TRUE;
		}

		case DLL_PROCESS_DETACH: {
			auto fail = false;

			s_crashMessageBoxHandler.reset();

			if (const auto res = MH_Uninitialize(); res != MH_OK && res != MH_ERROR_NOT_INITIALIZED) {
				fail = true;
				Utils::Win32::DebugPrint(L"MH_Uninitialize error: {}", MH_StatusToString(res));
			}
			if (fail)
				TerminateProcess(GetCurrentProcess(), -1);
			return TRUE;
		}
	}
	return TRUE;
}

extern "C" int32_t __stdcall XA_Initialize(const LoaderInterface::LoaderInfo* pInfo) {
	if (!pInfo || pInfo->Size < offsetof(LoaderInterface::LoaderInfo, EnableInjectOnCreateProcess) || pInfo->Version < LoaderInterface::MinimumVersion) {
		Utils::Win32::DebugPrint(L"XA_Initialize: loader interface {} (size {}) is not supported; need {} or newer",
			pInfo ? pInfo->Version : 0, pInfo ? pInfo->Size : 0, LoaderInterface::MinimumVersion);
		return LoaderInterface::Incompatible;
	}

	try {
		{
			const auto lock = std::lock_guard(s_initializeMtx);
			if (s_initialized)
				return Dll::EnableXivAlexander(1) == 0 ? LoaderInterface::Initialized : LoaderInterface::Failed;

			s_initialized = true;
			StoreLoaderInfo(*pInfo);
		}

		const auto activationContextCleanup = Dll::ActivationContext().With();
		CheckObfuscatedArguments();
		s_crashMessageBoxHandler = std::make_unique<XivAlexander::Misc::CrashMessageBoxHandler>();

		if (Dll::IsLoadedFromEntryPoint() && IsGameProcess())
			InitializeGameBeforeEntryPoint();

		return Dll::EnableXivAlexander(1) == 0 ? LoaderInterface::Initialized : LoaderInterface::Failed;
	} catch (const std::exception& e) {
		Utils::Win32::DebugPrint(L"XA_Initialize error: {}", e.what());
		return LoaderInterface::Failed;
	}
}

// Game folder copies of XivAlexander64.dll from before the loader DLLs call this; the loader DLL does the rest.
void Dll::InjectEntryPoint(InjectEntryPointParameters* pParam) {
	try {
		const auto loader = Utils::Win32::LoadedModule(Module().PathOf().parent_path() / XivAlexLoaderDll64NameW);
		loader.SetPinned();
		loader.GetProcAddress<decltype(&InjectEntryPoint)>("XA_InjectEntryPoint")(pParam);
	} catch (const std::exception& e) {
		Utils::Win32::DebugPrint(L"InjectEntryPoint: cannot hand over to {}: {}", XivAlexLoaderDll64NameW, e.what());
		Utils::Win32::Process::Current().WriteMemory(pParam->EntryPoint, pParam->EntryPointOriginalBytes, pParam->EntryPointOriginalLength, true);
	}
}

size_t Dll::EnableInjectOnCreateProcess(size_t flags) {
	if (!s_loaderInfo.EnableInjectOnCreateProcess)
		return -1;
	return s_loaderInfo.EnableInjectOnCreateProcess(flags);
}

size_t Dll::DisableAllApps(void*) {
	if (const auto res = EnableXivAlexander(0))
		return res;

	EnableInjectOnCreateProcess(0);
	// Only a DLL loaded into a running process gets unloaded; the others stay and keep filtering.
	if (s_loaderInfo.Mode == LoaderInterface::LoadMode::Remote && XA_LOADERINFO_HAS(&s_loaderInfo, SetCreateProcessArgumentFilter) && s_loaderInfo.SetCreateProcessArgumentFilter)
		s_loaderInfo.SetCreateProcessArgumentFilter(nullptr);
	if (s_loaderInfo.ReleaseFeatures)
		s_loaderInfo.ReleaseFeatures();
	return 0;
}

void Dll::CallFreeLibrary(void*) {
	FreeLibraryAndExitThread(Module(), 0);
}

size_t Dll::DisableUnloading(const char* pszReason) {
	s_dllUnloadDisableReason = pszReason ? pszReason : "(reason not specified)";
	Module().SetPinned();
	return 0;
}

const char* Dll::GetUnloadDisabledReason() {
	return s_dllUnloadDisableReason.empty() ? nullptr : s_dllUnloadDisableReason.c_str();
}

LPCWSTR Dll::GetStringResFromId(UINT resId) {
	try {
		const auto conf = XivAlexander::Config::Acquire();
		return conf->Runtime.GetStringRes(resId);
	} catch (...) {
		return 1 + FindStringResourceEx(Module(), resId);
	}
}

std::wstring Dll::GetOriginalCommandLine() {
	return s_originalCommandLine;
}

std::wstring Dll::GetGameCommandLine() {
	return s_gameCommandLineW.empty() ? s_originalCommandLine : s_gameCommandLineW;
}

bool Dll::IsOriginalCommandLineObfuscated() {
	return s_originalCommandLineIsObfuscated;
}

void Dll::ReplaceOriginalCommandLine(XivAlexander::Game::CommandLine::ArgumentList params) {
	s_originalCommandLine = std::format(L"\"{}\" {}", Utils::Win32::Process::Current().PathOf().wstring(), Game::CommandLine::ToString(params, false));
	s_originalCommandLineIsObfuscated = false;
	SetGameCommandLine(std::move(params));
}

bool Dll::IsLanguageRegionModifiable() {
	static std::optional<bool> s_modifiable;
	if (!s_modifiable.has_value()) {
		try {
			s_modifiable = XivAlexander::Misc::GameInstallationDetector::GetGameReleaseInfo().Region == xivres::game_release_publisher::SquareEnix;
		} catch (...) {
			s_modifiable = false;
		}
	}
	return *s_modifiable;
}

bool Dll::IsLoadedFromEntryPoint() {
	return s_loaderInfo.Mode == LoaderInterface::LoadMode::EntryPoint || s_loaderInfo.Mode == LoaderInterface::LoadMode::Proxy;
}

HWND Dll::FindGameMainWindow(bool throwOnError) {
	HWND hwnd = nullptr;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid == GetCurrentProcessId())
			break;
	}
	if (hwnd == nullptr && throwOnError)
		throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_ERROR_GAME_WINDOW_NOT_FOUND) + 1));
	return hwnd;
}
