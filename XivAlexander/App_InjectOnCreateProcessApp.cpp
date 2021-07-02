#include "pch.h"
#include "App_InjectOnCreateProcessApp.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"
#include "App_XivAlexApp.h"
#include "App_InjectOnCreateProcessApp_x64.h"
#include "App_InjectOnCreateProcessApp_x86.h"

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

	Utils::Win32::Closeable::LoadedModule m_module;
	Utils::CallOnDestruction::Multiple m_cleanup;

public:
	Implementation()
		: m_module(LoadLibraryW(Utils::Win32::Process::Current().PathOf(g_hInstance).c_str()), nullptr) {
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

	void PostProcessExecution(DWORD dwCreationFlags, LPPROCESS_INFORMATION lpProcessInformation) {
		const auto process = Utils::Win32::Process(lpProcessInformation->hProcess, false);
		auto isXiv = false;
		try {
			auto pardir = process.PathOf().parent_path();

			// // check whether it's FFXIVQuickLauncher directory
			// isXiv |= exists(pardir / "XIVLauncher.exe");

			// check 3 parent directories to determine whether it might have anything to do with FFXIV
			for (int i = 0; i < 3 && !isXiv; ++i) {
				isXiv = exists(pardir / L"game" / XivAlex::GameExecutableNameW);
				pardir = pardir.parent_path();
			}

			if (isXiv) {
				if (process.IsProcess64Bits() == Utils::Win32::Process::Current().IsProcess64Bits()) {
					PatchEntryPointForInjection(process);
					
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
			L"IsXiv={}\n"
			L"Self: PID={}, Platform={}, Path={}\n"
			L"Target: PID={}, Platform={}, Path={}\n",
			isXiv,
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
	: m_detectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
	, m_pImpl(std::make_unique<Implementation>()) {
}

App::InjectOnCreateProcessApp::~InjectOnCreateProcessApp() = default;

static std::unique_ptr<App::InjectOnCreateProcessApp> s_injectOnCreateProcessApp;

extern "C" __declspec(dllexport) int __stdcall EnableInjectOnCreateProcess(size_t bEnable) {
	if (!!bEnable == !!s_injectOnCreateProcessApp)
		return 0;
	try {
		s_injectOnCreateProcessApp = bEnable ? std::make_unique<App::InjectOnCreateProcessApp>() : nullptr;
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(std::format("EnableInjectOnCreateProcessApp error: {}\n", e.what()).c_str());
		return -1;
	}
}

static void RunBeforeAppInit() {
	auto filename = Utils::Win32::Process::Current().PathOf().filename().wstring();
	CharLowerW(&filename[0]);
	if (filename == XivAlex::GameExecutableNameW) {
		std::thread([]() {
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

				EnableXivAlexander(1);

			} catch (std::exception& e) {
				Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
					L"Failed to load XivAlexander into the game process: {}\n\n"
					L"Process ID: {}",
					e.what(), GetCurrentProcessId());
			}

			return 0;
			}).detach();
	} else
		s_injectOnCreateProcessApp = std::make_unique<App::InjectOnCreateProcessApp>();
}

extern "C" __declspec(dllexport) void __stdcall InjectEntryPoint(InjectEntryPointParameters * p) {
	// conjecture: not going to allocate stack or something yet, so must use the minimum; run stuff on thread
	// maybe figure out why won't it work if the work isn't done on a separate thread (with a separate stack)
	const auto h = CreateThread(nullptr, 0, [](void* param_) -> DWORD {
		// create copy, since we are going to do VirtualFree on the address containing param soon
		const InjectEntryPointParameters param = *static_cast<InjectEntryPointParameters*>(param_);

		DWORD dummy;
		memcpy(param.EntryPoint, param.EntryPointOriginalBytes, param.EntryPointOriginalLength);
		FlushInstructionCache(GetCurrentProcess(), param.EntryPoint, param.EntryPointOriginalLength);
		VirtualProtect(param.EntryPoint, param.EntryPointOriginalLength, PAGE_EXECUTE_READ, &dummy);
		VirtualFree(param.TrampolineAddress, 0, MEM_RELEASE);

		RunBeforeAppInit();
		FreeLibraryAndExitThread(g_hInstance, 0);
		}, p, 0, nullptr);
	assert(h);
	WaitForSingleObject(h, INFINITE);
	CloseHandle(h);
}

extern "C" __declspec(dllexport) int __stdcall PatchEntryPointForInjection(HANDLE hProcess) {
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
