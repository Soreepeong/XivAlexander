#include "pch.h"
#include "App_InjectOnCreateProcessApp.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"
#include "XivAlexander/XivAlexander.h"

#if INTPTR_MAX == INT64_MAX
#include "App_InjectOnCreateProcessApp_x64.h"
#elif INTPTR_MAX == INT32_MAX
#include "App_InjectOnCreateProcessApp_x86.h"
#endif 

class App::InjectOnCreateProcessApp::Implementation {

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

public:
	bool InjectAll = false;
	bool InjectGameOnly = false;

	Implementation() {
		m_cleanup += CreateProcessW.SetHook([this](_In_opt_ LPCWSTR lpApplicationName, _Inout_opt_ LPWSTR lpCommandLine, _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes, _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes, _In_ BOOL bInheritHandles, _In_ DWORD dwCreationFlags, _In_opt_ LPVOID lpEnvironment, _In_opt_ LPCWSTR lpCurrentDirectory, _In_ LPSTARTUPINFOW lpStartupInfo, _Out_ LPPROCESS_INFORMATION lpProcessInformation) -> BOOL {
			const bool noOperation = dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);
			const auto result = CreateProcessW.bridge(
				lpApplicationName,
				lpCommandLine,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags | (noOperation ? 0 : CREATE_SUSPENDED),
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo,
				lpProcessInformation);
			if (result && !noOperation)
				PostProcessExecution(dwCreationFlags, lpProcessInformation);
			return result;
			});
		m_cleanup += CreateProcessA.SetHook([this](_In_opt_ LPCSTR lpApplicationName, _Inout_opt_ LPSTR lpCommandLine, _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes, _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes, _In_ BOOL bInheritHandles, _In_ DWORD dwCreationFlags, _In_opt_ LPVOID lpEnvironment, _In_opt_ LPCSTR lpCurrentDirectory, _In_ LPSTARTUPINFOA lpStartupInfo, _Out_ LPPROCESS_INFORMATION lpProcessInformation) -> BOOL {
			const bool noOperation = dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS);
			const auto result = CreateProcessA.bridge(
				lpApplicationName,
				lpCommandLine,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags | (noOperation ? 0 : CREATE_SUSPENDED),
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo,
				lpProcessInformation);
			if (result && !noOperation)
				PostProcessExecution(dwCreationFlags, lpProcessInformation);
			return result;
			});
	}

	~Implementation() = default;

	[[nodiscard]] bool IsInjectTarget(const Utils::Win32::Process& process) const {
		const auto path = process.PathOf();
		auto pardir = path.parent_path();
		const auto filename = path.filename();
		if (equivalent(pardir, Dll::Module().PathOf().parent_path())
			&& (filename == XivAlex::XivAlexLoader32NameW || filename == XivAlex::XivAlexLoader64NameW))
			return false;

		if (InjectAll)
			return true;

		if (InjectGameOnly)
			return filename == XivAlex::GameExecutable32NameW || filename == XivAlex::GameExecutable64NameW;

		// check 3 parent directories to determine whether it might have anything to do with FFXIV
		for (int i = 0; i < 3; ++i) {
			if (exists(pardir / L"game" / XivAlex::GameExecutableNameW))
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
					LaunchXivAlexLoaderWithTargetHandles({ process }, XivAlexDll::LoaderAction::Internal_Inject_HookEntryPoint, true, 
						Dll::Module().PathOf().parent_path() / XivAlex::XivAlexLoaderOppositeNameW);
				}
			}
		} catch (std::exception& e) {
			error = Utils::FromUtf8(e.what());
		}
		
#ifdef _DEBUG
		error += L" _DEBUG";
#endif
		if (!error.empty()) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
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

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::EnableInjectOnCreateProcess(size_t flags) {
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

static void InitializeBeforeOriginalEntryPoint(HANDLE hContinuableEvent) {
	const auto process = Utils::Win32::Process::Current();
	auto filename = process.PathOf().filename().wstring();
	CharLowerW(&filename[0]);
	s_injectOnCreateProcessApp = std::make_unique<App::InjectOnCreateProcessApp>();

	if (filename != XivAlex::GameExecutableNameW)
		return; // not the game process; don't load XivAlex app

	// the game might restart itself for whatever reason.
	s_injectOnCreateProcessApp->SetFlags(XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly);

	try {
		bool wasObfuscated;
		const auto pairs = XivAlex::ParseGameCommandLine(Utils::ToUtf8(Utils::Win32::GetCommandLineWithoutProgramName()), &wasObfuscated);
		if (wasObfuscated) {
			for (const auto& [k, v] : pairs) {
				if (k == "T") {
					const auto time = std::strtol(&v[0], nullptr, 10);
					const auto now = static_cast<decltype(time)>(GetTickCount64());
					if (now - time > 20000) {
						// if UAC dialog took a long time, then execute again
						if (Utils::Win32::RunProgram({
							.path = process.PathOf(),
							.args = Utils::FromUtf8(XivAlex::CreateGameCommandLine(pairs, true)),
						})) {
#ifdef _DEBUG
							Utils::Win32::MessageBoxF(nullptr, MB_OK, L"XivAlexander", L"Restarting due to tick count difference of {} between encryption timestamp and now.", now - time);
#endif
							TerminateProcess(GetCurrentProcess(), 0);
						}
					}
				}
			}
		}
	} catch (...) {
		// do nothing
	}

	// let original entry point continue execution.
	SetEvent(hContinuableEvent);

	if (WaitForInputIdle(GetCurrentProcess(), 10000) == WAIT_TIMEOUT)
		throw std::runtime_error("Timed out waiting for the game to run.");

	HWND hwnd = nullptr;
	while (WaitForSingleObject(GetCurrentProcess(), 100) == WAIT_TIMEOUT) {
		hwnd = nullptr;
		while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
			DWORD pid;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid == GetCurrentProcessId())
				break;
		}
		if (hwnd && IsWindowVisible(hwnd))
			break;
	}

	if (!hwnd)
		return;
	
	XivAlexDll::EnableXivAlexander(1);
    XivAlexDll::EnableInjectOnCreateProcess(0);
}

extern "C" __declspec(dllexport) void __stdcall XivAlexDll::InjectEntryPoint(InjectEntryPointParameters * pParam) {
	pParam->Internal.hContinuableEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	pParam->Internal.hWorkerThread = CreateThread(nullptr, 0, [](void* pParam) -> DWORD {
		const auto process = Utils::Win32::Process::Current();
		const auto activationContextCleanup = Dll::ActivationContext().With();
		try {
			const auto p = static_cast<InjectEntryPointParameters*>(pParam);
			const auto hContinueNotify = Utils::Win32::Handle::DuplicateFrom<Utils::Win32::Event>(p->Internal.hContinuableEvent);
			process.WriteMemory(p->EntryPoint, p->EntryPointOriginalBytes, p->EntryPointOriginalLength, true);

#ifdef _DEBUG
			Utils::Win32::MessageBoxF(MB_OK, MB_OK, L"XivAlexander",
				L"PID: {}\nPath: {}\nCommand Line: {}", process.GetId(), process.PathOf().wstring(), GetCommandLineW());
#endif

			InitializeBeforeOriginalEntryPoint(hContinueNotify);
			SetEvent(hContinueNotify);
		} catch (std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
				L"Could not load this process.\nError: {}\n\nPID: {}\nPath: {}\nCommand Line: {}",
				e.what(), process.GetId(), process.PathOf().wstring(), GetCommandLineW());
			TerminateProcess(GetCurrentProcess(), 1);
		}
		FreeLibraryAndExitThread(Dll::Module(), 0);
		}, pParam, 0, nullptr);
	assert(pParam->Internal.hContinuableEvent);
	assert(pParam->Internal.hWorkerThread);
	WaitForSingleObject(pParam->Internal.hContinuableEvent, INFINITE);
	CloseHandle(pParam->Internal.hContinuableEvent);
	CloseHandle(pParam->Internal.hWorkerThread);

	// this invalidates "p" too
	VirtualFree(pParam->TrampolineAddress, 0, MEM_RELEASE);
}

XIVALEXANDER_DLLEXPORT void XivAlexDll::PatchEntryPointForInjection(HANDLE hProcess) {
	const auto process = Utils::Win32::Process(hProcess, false);

	const auto regions = process.GetCommittedImageAllocation();
	if (regions.empty())
		throw std::runtime_error("Could not find memory region of the program.");

	auto& mem = process.GetModuleMemoryBlockManager(static_cast<HMODULE>(regions.front().BaseAddress));

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
		trampoline = new (&trampolineBuffer[0]) TrampolineTemplate();
		pathBytesTarget = { &trampolineBuffer[trampolineLength], pathBytes.size_bytes() };
	}

	const auto rvaEntryPoint = mem.OptionalHeaderMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ? mem.OptionalHeader32.AddressOfEntryPoint : mem.OptionalHeader64.AddressOfEntryPoint;

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
}
