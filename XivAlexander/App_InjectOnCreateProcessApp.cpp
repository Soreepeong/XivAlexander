#include "pch.h"
#include "App_InjectOnCreateProcessApp.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Sqex_CommandLine.h>
#include <XivAlexanderCommon/Utils_CallOnDestruction.h>
#include <XivAlexanderCommon/Utils_Win32.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_ConfigRepository.h"
#include "App_DalamudHandlerApp.h"
#include "App_Feature_GameResourceOverrider.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"
#include "DllMain.h"
#include "resource.h"
#if INTPTR_MAX == INT64_MAX
#include "App_InjectOnCreateProcessApp_x64.h"
#elif INTPTR_MAX == INT32_MAX
#include "App_InjectOnCreateProcessApp_x86.h"
#endif

struct App::InjectOnCreateProcessApp::Implementation {

	Misc::Hooks::PointerFunction<BOOL, LPCWSTR,
		LPWSTR,
		LPSECURITY_ATTRIBUTES,
		LPSECURITY_ATTRIBUTES,
		BOOL,
		DWORD,
		LPVOID,
		LPCWSTR,
		LPSTARTUPINFOW,
		LPPROCESS_INFORMATION> CreateProcessW{ "CreateProcessW", ::CreateProcessW };

	Misc::Hooks::PointerFunction<BOOL, LPCSTR,
		LPSTR,
		LPSECURITY_ATTRIBUTES,
		LPSECURITY_ATTRIBUTES,
		BOOL,
		DWORD,
		LPVOID,
		LPCSTR,
		LPSTARTUPINFOA,
		LPPROCESS_INFORMATION> CreateProcessA{ "CreateProcessA", ::CreateProcessA };

	Utils::CallOnDestruction::Multiple m_cleanup;

	bool InjectAll = false;
	bool InjectGameOnly = false;

	Implementation();
	~Implementation();

	[[nodiscard]] bool IsInjectTarget(const Utils::Win32::Process& process) const {
		const auto path = process.PathOf();
		auto pardir = path.parent_path();
		const auto filename = path.filename().wstring();
		if (equivalent(pardir, Dll::Module().PathOf().parent_path())
			&& (lstrcmpiW(filename.c_str(), XivAlexDll::XivAlexLoader32NameW) == 0 || lstrcmpiW(filename.c_str(), XivAlexDll::XivAlexLoader64NameW) == 0))
			return false;

		const auto isGame32 = lstrcmpiW(filename.c_str(), XivAlexDll::GameExecutable32NameW) == 0;
		const auto isGame64 = lstrcmpiW(filename.c_str(), XivAlexDll::GameExecutable64NameW) == 0;

		if (isGame32) {
			for (const auto candidate : { "d3d9.dll", "dinput8.dll" }) {
				try {
					if (XivAlexDll::IsXivAlexanderDll(pardir / candidate))
						return false;
				} catch (...) {
					// pass
				}
			}
		} else if (isGame64) {
			for (const auto candidate : { "d3d11.dll", "dxgi.dll", "dinput8.dll" }) {
				try {
					if (XivAlexDll::IsXivAlexanderDll(pardir / candidate))
						return false;
				} catch (...) {
					// pass
				}
			}
		}

		if (InjectAll)
			return true;

		if (InjectGameOnly)
			return isGame32 || isGame64;

		// check 3 parent directories to determine whether it might have anything to do with FFXIV
		for (int i = 0; i < 3; ++i) {
			if (exists(pardir / L"game" / XivAlexDll::GameExecutableNameW))
				return true;
			pardir = pardir.parent_path();
		}
		return false;
	}

	void PostProcessExecution(DWORD dwCreationFlags, LPPROCESS_INFORMATION lpProcessInformation) {
		const auto activationContextCleanup = Dll::ActivationContext().With();

		const auto process = Utils::Win32::Process(lpProcessInformation->hProcess, false);
		auto isTarget = false;
		std::wstring error;
		try {
			if ((isTarget = IsInjectTarget(process))) {
				if (process.IsProcess64Bits() == Utils::Win32::Process::Current().IsProcess64Bits()) {
					XivAlexDll::PatchEntryPointForInjection(process);
				} else {
					LaunchXivAlexLoaderWithTargetHandles({ process }, XivAlexDll::LoaderAction::Internal_Inject_HookEntryPoint, true, {}, XivAlexDll::Opposite);
				}
			}
		} catch (const std::exception& e) {
			error = Utils::FromUtf8(e.what());
		}

#ifdef _DEBUG
		error += L" _DEBUG";
#endif
		if (!error.empty()) {
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
				L"Error: {}\n\n"
				L"isTarget={}\n"
				L"Self: PID={}, Platform={}, Path={}\n"
				L"Target: PID={}, Platform={}, Path={}\n",
				error, isTarget,
				Utils::Win32::Process::Current().GetId(),
				Utils::Win32::Process::Current().IsProcess64Bits() ? L"x64" : L"x86",
				Utils::Win32::Process::Current().PathOf(),
				process.GetId(), process.IsProcess64Bits() ? L"x64" : L"x86", process.PathOf()
			);
		}
		if (!(dwCreationFlags & CREATE_SUSPENDED))
			ResumeThread(lpProcessInformation->hThread);
	}
};

App::InjectOnCreateProcessApp::Implementation::Implementation() {
	m_cleanup += CreateProcessW.SetHook([this](_In_opt_ LPCWSTR lpApplicationName, _Inout_opt_ LPWSTR lpCommandLine, _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes, _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes, _In_ BOOL bInheritHandles, _In_ DWORD dwCreationFlags, _In_opt_ LPVOID lpEnvironment, _In_opt_ LPCWSTR lpCurrentDirectory, _In_ LPSTARTUPINFOW lpStartupInfo, _Out_ LPPROCESS_INFORMATION lpProcessInformation) -> BOOL {
		int result = 0;
		const bool noOperation = dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);
		std::filesystem::path applicationPath;
		std::wstring commandLine, pathForCommandLine;
		std::wstring buf;
		try {
			if (lpCommandLine)
				std::tie(pathForCommandLine, commandLine) = Utils::Win32::SplitCommandLineIntoNameAndArgs(lpCommandLine);
			else if (lpApplicationName)
				pathForCommandLine = lpApplicationName;
			else
				throw Utils::Win32::Error(ERROR_INVALID_PARAMETER);

			if (lpApplicationName) {
				applicationPath = lpApplicationName;

			} else {
				if (lpCommandLine[0] == L'"') {
					applicationPath = pathForCommandLine;
				} else {
					const auto parts = Utils::StringSplit<std::wstring>(lpCommandLine, L" ");
					std::wstring applicationPathBuf = parts[0];
					size_t i = 0;
					while (true) {
						std::filesystem::path applicationPathConstructing = applicationPathBuf;
						if (!applicationPathConstructing.has_extension())
							applicationPathConstructing += ".exe";
						if (!applicationPathConstructing.is_absolute()) {
							buf.resize(PATHCCH_MAX_CCH);
							buf.resize(SearchPathW(nullptr, applicationPathBuf.c_str(), L".exe", PATHCCH_MAX_CCH, &buf[0], nullptr));
							if (!buf.empty())
								applicationPathConstructing = buf;
						}
						if (exists(applicationPathConstructing)) {
							applicationPath = std::move(applicationPathConstructing);
							pathForCommandLine = applicationPath;
							commandLine.clear();
							while (++i < parts.size()) {
								if (!commandLine.empty())
									commandLine.push_back(L' ');
								commandLine.append(parts[i]);
							}
							break;
						}
						if (++i >= parts.size())
							throw Utils::Win32::Error(ERROR_FILE_NOT_FOUND);
						applicationPathBuf.push_back(L' ');
						applicationPathBuf.append(parts[i]);
					}
				}
			}

			buf.resize(PATHCCH_MAX_CCH);
			buf.resize(GetFullPathNameW(applicationPath.c_str(), PATHCCH_MAX_CCH, &buf[0], nullptr));
			if (buf.empty())
				throw Utils::Win32::Error("GetFullPathNameW");
			else
				applicationPath = buf;

			try {
				if (equivalent(applicationPath, Utils::Win32::Process::Current().PathOf())
					&& (lstrcmpiW(applicationPath.filename().c_str(), XivAlexDll::GameExecutable32NameW) == 0
						|| lstrcmpiW(applicationPath.filename().c_str(), XivAlexDll::GameExecutable64NameW) == 0)) {
					if (Dll::IsOriginalCommandLineObfuscated())
						commandLine = Sqex::CommandLine::ToString(Sqex::CommandLine::FromString(std::format(L"unused.exe {}", commandLine)), true);
				}
			} catch (...) {
				// pass
			}

			if (!commandLine.empty())
				commandLine = std::format(L"{} {}", Utils::Win32::ReverseCommandLineToArgv(pathForCommandLine), commandLine);
			else
				commandLine = Utils::Win32::ReverseCommandLineToArgv(pathForCommandLine);

			result = CreateProcessW.bridge(
				lpApplicationName ? applicationPath.c_str() : nullptr,
				lpCommandLine ? commandLine.data() : nullptr,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags | (noOperation ? 0 : CREATE_SUSPENDED),
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo,
				lpProcessInformation);
			if (!result)
				throw Utils::Win32::Error("CreateProcessW");

#ifdef _DEBUG
			Dll::MessageBoxF(nullptr, MB_OK,
				L"CreateProcessW OK\n\n"
				L"lpApplicationName={}\n"
				"lpCommandLine={}\n"
				"resolved lpApplicationName={}\n"
				"resolved lpCommandLine={}\n",
				lpApplicationName ? lpApplicationName : L"(nullptr)",
				lpCommandLine ? lpCommandLine : L"(nullptr)",
				lpApplicationName ? applicationPath.c_str() : L"(nullptr)",
				lpCommandLine ? commandLine.c_str() : L"(nullptr)"
			);
#endif

		} catch (const Utils::Win32::Error& e) {
#ifdef _DEBUG
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
				L"CreateProcessW failure: {}\n\n"
				L"lpApplicationName={}\n"
				"lpCommandLine={}\n"
				"resolved lpApplicationName={}\n"
				"resolved lpCommandLine={}\n",
				e.what(),
				lpApplicationName ? lpApplicationName : L"(nullptr)",
				lpCommandLine ? lpCommandLine : L"(nullptr)",
				lpApplicationName ? applicationPath.c_str() : L"(nullptr)",
				lpCommandLine ? commandLine.c_str() : L"(nullptr)"
			);
#endif
			SetLastError(e.Code());
			return FALSE;
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
				L"CreateProcessW failure: {}\n\n"
				L"lpApplicationName={}\n"
				"lpCommandLine={}\n"
				"resolved lpApplicationName={}\n"
				"resolved lpCommandLine={}\n",
				e.what(),
				lpApplicationName ? lpApplicationName : L"(nullptr)",
				lpCommandLine ? lpCommandLine : L"(nullptr)",
				lpApplicationName ? applicationPath.c_str() : L"(nullptr)",
				lpCommandLine ? commandLine.c_str() : L"(nullptr)"
			);
			SetLastError(ERROR_INTERNAL_ERROR);
			return FALSE;
		}
		if (!noOperation)
			PostProcessExecution(dwCreationFlags, lpProcessInformation);
		return result;
		});
	m_cleanup += CreateProcessA.SetHook([this](_In_opt_ LPCSTR lpApplicationName, _Inout_opt_ LPSTR lpCommandLine, _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes, _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes, _In_ BOOL bInheritHandles, _In_ DWORD dwCreationFlags, _In_opt_ LPVOID lpEnvironment, _In_opt_ LPCSTR lpCurrentDirectory, _In_ LPSTARTUPINFOA lpStartupInfo, _Out_ LPPROCESS_INFORMATION lpProcessInformation) -> BOOL {
		if (!lpStartupInfo) {
			SetLastError(ERROR_INVALID_PARAMETER);
			return 0;
		}

		std::vector<uint8_t> buf;
		buf.resize(lpStartupInfo->cb);
		memcpy(&buf[0], lpStartupInfo, lpStartupInfo->cb);

		STARTUPINFOW& siw = *reinterpret_cast<STARTUPINFOW*>(&buf[0]);
		std::optional<std::wstring> applicationName, commandLine, currentDirectory, reserved, desktop, title;
		if (lpApplicationName)
			applicationName = Utils::FromUtf8(lpApplicationName, CP_OEMCP);
		if (lpCommandLine)
			commandLine = Utils::FromUtf8(lpCommandLine, CP_OEMCP);
		if (lpCurrentDirectory)
			currentDirectory = Utils::FromUtf8(lpCurrentDirectory, CP_OEMCP);
		if (lpStartupInfo->lpReserved)
			reserved = Utils::FromUtf8(lpStartupInfo->lpReserved, CP_OEMCP);
		if (lpStartupInfo->lpDesktop)
			desktop = Utils::FromUtf8(lpStartupInfo->lpDesktop, CP_OEMCP);
		if (lpStartupInfo->lpTitle)
			title = Utils::FromUtf8(lpStartupInfo->lpTitle, CP_OEMCP);
		siw.lpReserved = reserved ? reserved->data() : nullptr;
		siw.lpDesktop = desktop ? desktop->data() : nullptr;
		siw.lpTitle = title ? title->data() : nullptr;
#ifdef _DEBUG
		Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
			L"CreateProcessA\n\n"
			L"lpApplicationName={}\n"
			"lpCommandLine={}\n",
			applicationName ? applicationName->c_str() : L"(nullptr)",
			commandLine ? commandLine->c_str() : L"(nullptr)"
		);
#endif

		return CreateProcessW(
			applicationName ? applicationName->data() : nullptr,
			commandLine ? commandLine->data() : nullptr,
			lpProcessAttributes,
			lpThreadAttributes,
			bInheritHandles,
			dwCreationFlags,
			lpEnvironment,
			currentDirectory ? currentDirectory->data() : nullptr,
			&siw,
			lpProcessInformation);
		});
}

App::InjectOnCreateProcessApp::Implementation::~Implementation() = default;

App::InjectOnCreateProcessApp::InjectOnCreateProcessApp()
	: m_module(Utils::Win32::LoadedModule::LoadMore(Dll::Module()))
	, m_detectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
	, m_pImpl(std::make_unique<Implementation>()) {
}

App::InjectOnCreateProcessApp::~InjectOnCreateProcessApp() = default;

void App::InjectOnCreateProcessApp::SetFlags(size_t flags) {
	m_pImpl->InjectAll = flags & XivAlexDll::InjectOnCreateProcessAppFlags::InjectAll;
	m_pImpl->InjectGameOnly = flags & XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly;
}

static std::unique_ptr<App::InjectOnCreateProcessApp> s_injectOnCreateProcessApp;

size_t __stdcall XivAlexDll::EnableInjectOnCreateProcess(size_t flags) {
	const bool use = flags & InjectOnCreateProcessAppFlags::Use;
	if (use == !!s_injectOnCreateProcessApp) {
		if (s_injectOnCreateProcessApp)
			s_injectOnCreateProcessApp->SetFlags(flags);
		return 0;
	}
	try {
		s_injectOnCreateProcessApp = use ? std::make_unique<App::InjectOnCreateProcessApp>() : nullptr;
		if (use)
			s_injectOnCreateProcessApp->SetFlags(flags);
		return 0;
	} catch (const std::exception& e) {
		Utils::Win32::DebugPrint(L"EnableInjectOnCreateProcessApp error: {}\n", e.what());
		return -1;
	}
}

static void InitializeAsStubBeforeOriginalEntryPoint() {
	const auto conf = App::Config::Acquire();
	auto selfFileNameLower = Dll::Module().PathOf().filename().wstring();
	CharLowerW(&selfFileNameLower[0]);
	if (selfFileNameLower == L"d3d11.dll") {
		for (auto& path : conf->Runtime.ChainLoadPath_d3d11.Value())
			if (const Utils::Win32::LoadedModule mod = path.empty() ? nullptr : Utils::Win32::LoadedModule(App::Config::Config::TranslatePath(path), 0, false))
				mod.SetPinned();
	} else if (selfFileNameLower == L"d3d9.dll") {
		for (auto& path : conf->Runtime.ChainLoadPath_d3d9.Value())
			if (const Utils::Win32::LoadedModule mod = path.empty() ? nullptr : Utils::Win32::LoadedModule(App::Config::Config::TranslatePath(path), 0, false))
				mod.SetPinned();
	} else if (selfFileNameLower == L"dinput8.dll") {
		for (auto& path : conf->Runtime.ChainLoadPath_dinput8.Value())
			if (const Utils::Win32::LoadedModule mod = path.empty() ? nullptr : Utils::Win32::LoadedModule(App::Config::Config::TranslatePath(path), 0, false))
				mod.SetPinned();
	} else if (selfFileNameLower == L"dxgi.dll") {
		for (auto& path : conf->Runtime.ChainLoadPath_dxgi.Value())
			if (const Utils::Win32::LoadedModule mod = path.empty() ? nullptr : Utils::Win32::LoadedModule(App::Config::Config::TranslatePath(path), 0, false))
				mod.SetPinned();
	}

	Dll::DisableUnloading(std::format("Loaded as DLL dependency in place of {}", Dll::Module().PathOf().filename()).c_str());

	GetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr, 0);
	if (GetLastError() != ERROR_ENVVAR_NOT_FOUND)
		return;

	std::filesystem::path loadPath;
	try {
		const auto conf = App::Config::Acquire();
		loadPath = conf->Init.ResolveXivAlexInstallationPath() / XivAlexDll::XivAlexDllNameW;
		const auto loadTarget = Utils::Win32::LoadedModule(loadPath);
		const auto params = XivAlexDll::PatchEntryPointForInjection(GetCurrentProcess());
		params->SkipFree = true;
		loadTarget.SetPinned();
		loadTarget.GetProcAddress<decltype(&XivAlexDll::InjectEntryPoint)>("XA_InjectEntryPoint")(params);

	} catch (const std::exception& e) {
		const auto activationContextCleanup = Dll::ActivationContext().With();
		auto loop = true;
		while (loop) {
			const auto choice = Dll::MessageBoxF(
				nullptr, MB_ICONWARNING | MB_ABORTRETRYIGNORE,
				L"{}\nReason: {}\n\nPress Abort to exit.\nPress Retry to open XivAlexander help webpage.\nPress Ignore to skip loading XivAlexander.",
				loadPath.empty() ? L"Failed to resolve XivAlexander installation path." : std::format(L"Failed to load {}.", loadPath.wstring()),
				e.what());
			switch (choice) {
			case IDRETRY: {
				SHELLEXECUTEINFOW shex{};
				shex.cbSize = sizeof shex;
				shex.nShow = SW_SHOW;
				shex.lpFile = conf->Runtime.GetStringRes(IDS_URL_HELP);
				if (!ShellExecuteExW(&shex))
					Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, Utils::Win32::FormatWindowsErrorMessage(GetLastError()));
				break;
			}
			case IDIGNORE: {
				loop = false;
				break;
			}
			case IDABORT:
				TerminateProcess(GetCurrentProcess(), -1);
			}
		}
	}
}

static void InitializeBeforeOriginalEntryPoint() {
	const auto& process = Utils::Win32::Process::Current();
	auto filename = process.PathOf().filename().wstring();
	CharLowerW(&filename[0]);
	s_injectOnCreateProcessApp = std::make_unique<App::InjectOnCreateProcessApp>();

	Dll::SetLoadedFromEntryPoint();

	if (filename != XivAlexDll::GameExecutableNameW)
		return;  // not the game process; don't load XivAlex app

	// the game might restart itself for whatever reason.
	s_injectOnCreateProcessApp->SetFlags(XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly);

	// Load game resource overrider before the game starts to load files.
	App::Feature::GameResourceOverrider::Enable();

	// Delay Initialize call to Dalamud Boot if Dalamud is being used
	App::DalamudHandlerApp::LoadDalamudHandler();

	static Utils::CallOnDestruction::Multiple s_hooks;
	static App::Misc::Hooks::ImportedFunction<HANDLE, DWORD, BOOL, DWORD> s_OpenProcessForXiv{ "kernel32::OpenProcess", "kernel32.dll", "OpenProcess" };
	static App::Misc::Hooks::PointerFunction<HANDLE, DWORD, BOOL, DWORD> s_OpenProcess{ "OpenProcess", ::OpenProcess };
	s_hooks += s_OpenProcessForXiv.SetHook([](DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
		if (dwProcessId == GetCurrentProcessId()) {
			// Prevent game from restarting itself on startup
			if (dwDesiredAccess & PROCESS_VM_WRITE) {
				SetLastError(ERROR_ACCESS_DENIED);
				return HANDLE{};
			}
		}
		return s_OpenProcess.bridge(dwDesiredAccess, bInheritHandle, dwProcessId);
	});
	s_hooks += s_OpenProcess.SetHook([](DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId) {
		if (dwProcessId == GetCurrentProcessId()) {
			// Prevent Reloaded from tripping
			if (HANDLE h{}; DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, dwDesiredAccess, bInheritHandle, 0))
				return h;
			return HANDLE{};
		}
		return s_OpenProcess.bridge(dwDesiredAccess, bInheritHandle, dwProcessId);
	});

	if (App::Config::Acquire()->Runtime.UseMoreCpuTime) {
		static App::Misc::Hooks::PointerFunction<DWORD_PTR, HANDLE, DWORD_PTR> s_SetThreadAffinityMask{ "SetThreadAffinityMask", ::SetThreadAffinityMask };
		static App::Misc::Hooks::PointerFunction<void, LPSYSTEM_INFO> s_GetSystemInfo("GetSystemInfo", ::GetSystemInfo);
		static App::Misc::Hooks::PointerFunction<void, DWORD> s_Sleep("Sleep", ::Sleep);
		static App::Misc::Hooks::PointerFunction<DWORD, DWORD, BOOL> s_SleepEx("SleepEx", ::SleepEx);
		s_hooks += s_SetThreadAffinityMask.SetHook([](HANDLE h, DWORD_PTR d) { return static_cast<DWORD_PTR>(-1); });
		s_hooks += s_GetSystemInfo.SetHook([&](LPSYSTEM_INFO i) {
			s_GetSystemInfo.bridge(i);
			i->dwNumberOfProcessors = std::min(192UL, i->dwNumberOfProcessors);
			});
		static uint16_t counter = 0;
		s_hooks += s_Sleep.SetHook([&](DWORD i) {
			if (i)
				s_Sleep.bridge(i);
			else if (!++counter)
				SwitchToThread();
			});
		s_hooks += s_SleepEx.SetHook([&](DWORD i, BOOL bAlertable) {
			if (i || bAlertable)
				return s_SleepEx.bridge(i, bAlertable);

			if (!++counter)
				SwitchToThread();
			return 0UL;
			});
	}

	void(Utils::Win32::Thread(L"EnableXivAlexanderSoon", []() {
		Sleep(1000);
		XivAlexDll::EnableXivAlexander(1);
		XivAlexDll::EnableInjectOnCreateProcess(0);
		}));
}

void __stdcall XivAlexDll::InjectEntryPoint(InjectEntryPointParameters * pParam) {
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &pParam->Internal.hMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
	pParam->Internal.hWorkerThread = CreateThread(nullptr, 0, [](void* pParam) -> DWORD {
		Utils::Win32::SetThreadDescription(GetCurrentThread(), L"InjectEntryPoint::WorkerThread");
		// ReSharper disable once CppInitializedValueIsAlwaysRewritten
		auto skipFree = false;
		{
			const auto& process = Utils::Win32::Process::Current();

			try {
				const auto activationContextCleanup = Dll::ActivationContext().With();
				const auto p = static_cast<InjectEntryPointParameters*>(pParam);
				const auto hMainThread = Utils::Win32::Thread(p->Internal.hMainThread, true);

				skipFree = p->SkipFree;

#ifdef _DEBUG
				Dll::MessageBoxF(nullptr, MB_OK,
					L"PID: {}\nPath: {}\nCommand Line: {}", process.GetId(), process.PathOf().wstring(), Dll::GetOriginalCommandLine());
#endif

				process.WriteMemory(p->EntryPoint, p->EntryPointOriginalBytes, p->EntryPointOriginalLength, true);
				if (p->LoadInstalledXivAlexDllOnly)
					InitializeAsStubBeforeOriginalEntryPoint();
				else
					InitializeBeforeOriginalEntryPoint();
			} catch (const std::exception& e) {
				Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR,
					1 + FindStringResourceEx(Dll::Module(), IDS_ERROR_INJECT),
					e.what(), process.GetId(), process.PathOf().wstring(), Dll::GetOriginalCommandLine());

#ifdef _DEBUG
				throw;
#else
				TerminateProcess(GetCurrentProcess(), 1);
#endif
			}
		}

		if (!skipFree) {
			// All stack allocated objects must be gone at this point
			FreeLibraryAndExitThread(Dll::Module(), 0);
		}
		return 0;
		}, pParam, 0, nullptr);
	assert(pParam->Internal.hWorkerThread);
	WaitForSingleObject(pParam->Internal.hWorkerThread, INFINITE);
	CloseHandle(pParam->Internal.hWorkerThread);

	// this invalidates "p" too
	VirtualFree(pParam->TrampolineAddress, 0, MEM_RELEASE);
}

XivAlexDll::InjectEntryPointParameters* XivAlexDll::PatchEntryPointForInjection(HANDLE hProcess) {
	const auto process = Utils::Win32::Process(hProcess, false);

	void* pBaseAddress;

	if (GetCurrentProcessId() == GetProcessId(hProcess))
		pBaseAddress = GetModuleHandleW(nullptr);
	else {
		const auto regions = process.GetCommittedImageAllocation();
		if (regions.empty())
			throw std::runtime_error("GetCommittedImageAllocation");
		pBaseAddress = regions.front().BaseAddress;
	}

	const auto& mem = process.GetModuleMemoryBlockManager(static_cast<HMODULE>(pBaseAddress));

	auto path = Dll::Module().PathOf().wstring();
	path.resize(path.size() + 1);  // add null character
	const auto pathBytes = std::span(reinterpret_cast<const uint8_t*>(path.c_str()), path.size() * sizeof path[0]);

	std::vector<uint8_t> trampolineBuffer;
	TrampolineTemplate* trampoline;
	std::span<uint8_t> pathBytesTarget;
	{
		constexpr auto trampolineLength = (sizeof TrampolineTemplate + sizeof size_t - 1) / sizeof size_t * sizeof size_t;
		trampolineBuffer.resize(0
			+ trampolineLength
			+ pathBytes.size_bytes()
		);
		trampoline = new(&trampolineBuffer[0]) TrampolineTemplate();
		pathBytesTarget = { &trampolineBuffer[trampolineLength], pathBytes.size_bytes() };
	}

	const auto rvaEntryPoint = mem.OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
		? mem.OptionalHeader32.AddressOfEntryPoint  // NOLINT(bugprone-branch-clone)
		: mem.OptionalHeader64.AddressOfEntryPoint;

	std::copy_n(pathBytes.begin(), pathBytesTarget.size_bytes(), pathBytesTarget.begin());
	process.ReadMemory(mem.CurrentModule, rvaEntryPoint,
		std::span(trampoline->buf_EntryPointBackup, sizeof trampoline->buf_EntryPointBackup));

	const auto pRemote = process.VirtualAlloc<char>(nullptr, trampolineBuffer.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

	trampoline->parameters = {
		.EntryPoint = reinterpret_cast<char*>(mem.CurrentModule) + rvaEntryPoint,
		.EntryPointOriginalBytes = pRemote + offsetof(TrampolineTemplate, buf_EntryPointBackup),
		.EntryPointOriginalLength = sizeof trampoline->buf_EntryPointBackup,
		.TrampolineAddress = pRemote,
	};
	trampoline->CallLoadLibrary.lpLibFileName.val = pRemote + (&pathBytesTarget[0] - &trampolineBuffer[0]);
	trampoline->CallLoadLibrary.fn.ptr = LoadLibraryW;
	trampoline->CallGetProcAddress.lpProcName.val = pRemote + offsetof(TrampolineTemplate, buf_CallGetProcAddress_lpProcName);
	trampoline->CallGetProcAddress.fn.ptr = GetProcAddress;
	trampoline->CallInjectEntryPoint.param.val = pRemote + offsetof(TrampolineTemplate, parameters);

	process.WriteMemory(pRemote, 0, std::span(trampolineBuffer));

	EntryPointThunkTemplate thunk{};
	thunk.CallTrampoline.fn.ptr = pRemote;

	process.WriteMemory(mem.CurrentModule, rvaEntryPoint, thunk, true);
	return &reinterpret_cast<TrampolineTemplate*>(pRemote)->parameters;
}
