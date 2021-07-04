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
		if (InjectAll)
			return true;

		if (InjectGameOnly) {
			auto filename = process.PathOf().filename().wstring();
			CharLowerW(&filename[0]);
			return filename == XivAlex::GameExecutable32NameW || filename == XivAlex::GameExecutable64NameW;
		}

		auto pardir = process.PathOf().parent_path();

		// // check whether it's FFXIVQuickLauncher directory
		// isXiv |= exists(pardir / "XIVLauncher.exe");

		// check 3 parent directories to determine whether it might have anything to do with FFXIV
		for (int i = 0; i < 3; ++i) {
			if (exists(pardir / L"game" / XivAlex::GameExecutableNameW))
				return true;
			pardir = pardir.parent_path();
		}
		return false;
	}

	void PostProcessExecution(DWORD dwCreationFlags, LPPROCESS_INFORMATION lpProcessInformation) {
		const auto process = Utils::Win32::Process(lpProcessInformation->hProcess, false);
		const auto isTarget = IsInjectTarget(process);
		try {
			if (isTarget) {
				if (process.IsProcess64Bits() == Utils::Win32::Process::Current().IsProcess64Bits()) {
					XivAlexDll::PatchEntryPointForInjection(process);

				} else {
					const auto companion = Utils::Win32::Process::Current().PathOf(g_hInstance).parent_path() / (process.IsProcess64Bits() ? XivAlex::XivAlexLoader64NameW : XivAlex::XivAlexLoader32NameW);
					if (!exists(companion))
						throw std::runtime_error("loader not found");

					Utils::Win32::Closeable::Handle hInheritableTargetProcessHandle;
					if (HANDLE h; !DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &h, 0, TRUE, DUPLICATE_SAME_ACCESS))
						throw Utils::Win32::Error("DuplicateHandle1");
					else
						hInheritableTargetProcessHandle.Attach(h, true);

					Utils::Win32::Closeable::Handle hStdinRead, hStdinWrite;
					if (HANDLE r, w; !CreatePipe(&r, &w, nullptr, 0))
						throw Utils::Win32::Error("CreatePipe");
					else {
						hStdinRead.Attach(r, true);
						hStdinWrite.Attach(w, true);
					}

					Utils::Win32::Closeable::Handle hInheritableStdinRead;
					if (HANDLE h; !DuplicateHandle(GetCurrentProcess(), hStdinRead, GetCurrentProcess(), &h, 0, TRUE, DUPLICATE_SAME_ACCESS))
						throw Utils::Win32::Error("DuplicateHandle2");
					else
						hInheritableStdinRead.Attach(h, true);

					Utils::Win32::Closeable::Handle companionProcess;
					{
						STARTUPINFOW si{};
						PROCESS_INFORMATION pi{};

						si.cb = sizeof si;
						si.dwFlags = STARTF_USESTDHANDLES;
						si.hStdInput = hInheritableStdinRead;
						si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
						si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

						auto args = std::format(L"\"{}\" --inject-into-stdin-handle", companion);
						if (!CreateProcessW.bridge(companion.c_str(), &args[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
							throw Utils::Win32::Error("CreateProcess");

						assert(pi.hProcess);
						assert(pi.hThread);

						companionProcess = Utils::Win32::Closeable::Handle(pi.hProcess, true);
						CloseHandle(pi.hThread);
					}

					const auto handleNumber = static_cast<uint64_t>(reinterpret_cast<size_t>(static_cast<HANDLE>(hInheritableTargetProcessHandle)));
					static_assert(sizeof handleNumber == 8);
					DWORD written;
					if (!WriteFile(hStdinWrite, &handleNumber, sizeof handleNumber, &written, nullptr) || written != sizeof handleNumber)
						throw Utils::Win32::Error("WriteFile");

					WaitForSingleObject(companionProcess, INFINITE);
				}
			}
		} catch (std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
				L"Failed to load XivAlexander into child process: {}\n\n"
				L"Process ID: {}",
				e.what(), lpProcessInformation->dwProcessId);
		}
#ifdef _DEBUG
		Utils::Win32::MessageBoxF(
			nullptr, MB_OK,
			L"XivAlexander",
			L"isTarget={}\n"
			L"Self: PID={}, Platform={}, Path={}\n"
			L"Target: PID={}, Platform={}, Path={}\n",
			isTarget,
			Utils::Win32::Process::Current().GetId(),
			Utils::Win32::Process::Current().IsProcess64Bits() ? L"x64" : L"x86",
			Utils::Win32::Process::Current().PathOf(),
			process.GetId(), process.IsProcess64Bits() ? L"x64" : L"x86", process.PathOf()
		);
#endif
		if (!(dwCreationFlags & CREATE_SUSPENDED))
			ResumeThread(lpProcessInformation->hThread);
	}
};

App::InjectOnCreateProcessApp::InjectOnCreateProcessApp()
	: m_module(Utils::Win32::Closeable::LoadedModule::From(g_hInstance))
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
	const bool use = flags & XivAlexDll::InjectOnCreateProcessAppFlags::Use;
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
		OutputDebugStringA(std::format("EnableInjectOnCreateProcessApp error: {}\n", e.what()).c_str());
		return -1;
	}
}

static void InitializeBeforeOriginalEntryPoint(HANDLE hContinuableEvent) {
	const auto process = Utils::Win32::Process::Current();	auto filename = process.PathOf().filename().wstring();
	CharLowerW(&filename[0]);
	s_injectOnCreateProcessApp = std::make_unique<App::InjectOnCreateProcessApp>();

	if (filename != XivAlex::GameExecutableNameW)
		return; // not the game process; don't load XivAlex app

	// the game might restart itself for whatever reason.
	s_injectOnCreateProcessApp->SetFlags(XivAlexDll::InjectOnCreateProcessAppFlags::InjectGameOnly);

	// let original entry point continue execution.
	SetEvent(hContinuableEvent);

	try {
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
			throw std::runtime_error("Game process exited before initialization.");
		XivAlexDll::EnableXivAlexander(1);

	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
			L"Failed to load XivAlexander into the game process: {}\n\n"
			L"Process ID: {}",
			e.what(), GetCurrentProcessId());
	}
}

extern "C" __declspec(dllexport) void __stdcall XivAlexDll::InjectEntryPoint(InjectEntryPointParameters * pParam__) {
	pParam__->Internal.hContinuableEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	pParam__->Internal.hWorkerThread = CreateThread(nullptr, 0, [](void* pParam_) -> DWORD {
		const auto process = Utils::Win32::Process::Current();
		try {
			const auto pParam = static_cast<InjectEntryPointParameters*>(pParam_);
			Utils::Win32::Closeable::Handle hContinueNotify;
			hContinueNotify.DuplicateFrom(pParam->Internal.hContinuableEvent);
			process.WriteMemory(pParam->EntryPoint, pParam->EntryPointOriginalBytes, pParam->EntryPointOriginalLength);
			process.FlushInstructionsCache(pParam->EntryPoint, pParam->EntryPointOriginalLength);
			process.VirtualProtect(pParam->EntryPoint, 0, pParam->EntryPointOriginalLength, PAGE_EXECUTE_READ);

#ifdef _DEBUG
			MessageBoxW(nullptr, std::format(L"PID: {}\nPath: {}\nCommand Line: {}", process.GetId(), process.PathOf().wstring(), GetCommandLineW()).c_str(), L"Injected EntryPoint", MB_OK);
#endif

			InitializeBeforeOriginalEntryPoint(hContinueNotify);
			SetEvent(hContinueNotify);
		} catch (std::exception& e) {
			MessageBoxW(nullptr, std::format(
				L"Could not load this process.\nError: {}\n\nPID: {}\nPath: {}\nCommand Line: {}", e.what(),
				process.GetId(), process.PathOf().wstring(), GetCommandLineW()
			).c_str(), L"XivAlexander Loader", MB_OK | MB_ICONERROR);
			TerminateProcess(GetCurrentProcess(), 1);
		}
		FreeLibraryAndExitThread(g_hInstance, 0);
		}, pParam__, 0, nullptr);
	assert(pParam__->Internal.hContinuableEvent);
	assert(pParam__->Internal.hWorkerThread);
	WaitForSingleObject(pParam__->Internal.hContinuableEvent, INFINITE);
	CloseHandle(pParam__->Internal.hContinuableEvent);
	CloseHandle(pParam__->Internal.hWorkerThread);

	// this invalidates "p" too
	VirtualFree(pParam__->TrampolineAddress, 0, MEM_RELEASE);
}

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::PatchEntryPointForInjection(HANDLE hProcess) {
	const auto process = Utils::Win32::Process(hProcess, false);

	try {
		const auto regions = process.GetCommittedImageAllocation();
		if (regions.empty())
			throw std::runtime_error("Could not find memory region of the program.");

		auto& mem = process.GetModuleMemoryBlockManager(static_cast<HMODULE>(regions.front().BaseAddress));

		auto path = Utils::Win32::Process::Current().PathOf(g_hInstance).wstring();
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
		process.VirtualProtect(mem.CurrentModule, rvaEntryPoint, sizeof thunk, PAGE_EXECUTE_READWRITE);
		process.WriteMemory(mem.CurrentModule, rvaEntryPoint, thunk);
		return 0;
	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
			L"Failed to load XivAlexander into child process: {}\n\n"
			L"Process ID: {}",
			e.what(), GetProcessId(process));
		return -1;
	}
}
