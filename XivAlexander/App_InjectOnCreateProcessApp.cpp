#include "pch.h"
#include "App_InjectOnCreateProcessApp.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"
#include "App_XivAlexApp.h"

static const uint8_t EntryPointThunkTemplate[]{
	/* 00 */ 0xFF, 0x15, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC,
	/* 08 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
	
	/*
	thunk:
	call QWORD PTR [rip+trampoline_address]
	int 3
	int 3

	trampoline_address:
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	 */
};

static const uint8_t TrampolineTemplate[]{
	/* 00 */ 0x59, 0x48, 0x83, 0xE9, 0x06, 0x51, 0x48, 0x81,
	/* 08 */ 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
	/* 10 */ 0x3C, 0x00, 0x00, 0x00, 0x48, 0x89, 0x01, 0x48,
	/* 18 */ 0x8B, 0x05, 0x3A, 0x00, 0x00, 0x00, 0x48, 0x89,
	/* 20 */ 0x41, 0x08, 0xE8, 0x21, 0x00, 0x00, 0x00, 0x48,
	/* 28 */ 0x83, 0xC1, 0x49, 0xFF, 0x15, 0x37, 0x00, 0x00,
	/* 30 */ 0x00, 0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,
	/* 38 */ 0xE8, 0x0B, 0x00, 0x00, 0x00, 0x48, 0x89, 0xCA,
	/* 40 */ 0x59, 0x51, 0xFF, 0x25, 0x18, 0x00, 0x00, 0x00,
	/* 48 */ 0x48, 0x8B, 0x0C, 0x24, 0xC3, 0xCC, 0xCC, 0xCC,
	/* 50 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
	/* 58 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
	/* 60 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
	/* 68 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
	
	/*
	restore:
	pop rcx
	sub rcx, 6
	push rcx

	sub rsp, 128
	mov rax, QWORD PTR [rip+OriginalEntryPoint]
	mov QWORD PTR [rcx], rax
	mov rax, QWORD PTR [rip+OriginalEntryPoint+8]
	mov QWORD PTR [rcx+8], rax

	execute:
	call get_eip
	add rcx, 0x49
	call QWORD PTR [rip+LoadLibraryW]

	add rsp, 128

	call get_eip
	mov rdx, rcx
	pop rcx
	push rcx
	jmp QWORD PTR [rip+EntryImplFromDll]

	get_eip:
	mov rcx, [rsp]
	ret

	int 3
	int 3
	int 3

	OriginalEntryPoint:
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3

	EntryImplFromDll:
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3

	LoadLibraryW:
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3
	int 3

	LoadLibraryW_Param1_wsz:
	int 3
	 */
};
static const auto TrampolineTemplateOriginalEntryPointOffset = 0x50;
static const auto TrampolineTemplateEntryImplFromDllOffset = 0x60;
static const auto TrampolineTemplateLoadLibraryWOffset = 0x68;

extern "C" __declspec(dllexport) void __stdcall InjectEntryPoint(void* originalEntryPoint, void* thunkAddress);

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
		: m_module(LoadLibraryW(Utils::Win32::Modules::PathFromModule(g_hInstance).c_str()), nullptr) {
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
				PostProcessExecution(lpCommandLine ? lpCommandLine : (lpApplicationName ? lpApplicationName : L""), dwCreationFlags, lpProcessInformation);
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
				PostProcessExecution(Utils::FromOem(lpCommandLine ? lpCommandLine : (lpApplicationName ? lpApplicationName : "")), dwCreationFlags, lpProcessInformation);
			return result;
			});
	}

	~Implementation() = default;

	void PostProcessExecution(const std::wstring& commandLine, DWORD dwCreationFlags, LPPROCESS_INFORMATION lpProcessInformation) {
		const auto hProcess = lpProcessInformation->hProcess;

		std::wstringstream log;
		
		try {
			int argc = 0;
			LPWSTR* argv = CommandLineToArgvW(&commandLine[0], &argc);
			if (!argv)
				throw Utils::Win32::Error("CommandLineToArgvW");
			const auto path = std::filesystem::path(argv[0]);
			LocalFree(argv);

			auto pardir = path.parent_path();
			bool isXiv = false;

			// check whether it's FFXIVQuickLauncher directory
			isXiv |= exists(pardir / "XIVLauncher.exe");

			// check 3 parent directories to determine whether it might have anything to do with FFXIV
			for (int i = 0; i < 3 && !isXiv; ++i) {
				isXiv = exists(pardir / "game" / XivAlex::GameExecutableName);
				pardir = pardir.parent_path();
			}

			if (isXiv) {
				const auto ntpath = Utils::Win32::ToNativePath(path);
				const auto ntpathw = ntpath.wstring();

				std::vector<MEMORY_BASIC_INFORMATION> regions;
				for (MEMORY_BASIC_INFORMATION mbi{};
					VirtualQueryEx(hProcess, mbi.BaseAddress, &mbi, sizeof mbi);
					mbi.BaseAddress = static_cast<char*>(mbi.BaseAddress) + mbi.RegionSize) {
					if (!(mbi.State & MEM_COMMIT) || mbi.Type != MEM_IMAGE)
						continue;
					if (Utils::Win32::GetMappedImageNativePath(hProcess, mbi.BaseAddress).wstring() == ntpathw)
						regions.emplace_back(mbi);
				}

				const auto base = static_cast<char*>(regions.front().BaseAddress);

				IMAGE_DOS_HEADER dos;
				if (!ReadProcessMemory(hProcess, base, &dos, sizeof dos, nullptr))
					throw Utils::Win32::Error("ReadProcessMemory1");

				IMAGE_NT_HEADERS nt;
				if (!ReadProcessMemory(hProcess, base + dos.e_lfanew, &nt, sizeof nt, nullptr))
					throw Utils::Win32::Error("ReadProcessMemory2");

				std::vector trampolineTemplate(TrampolineTemplate, TrampolineTemplate + sizeof TrampolineTemplate);
				if (!ReadProcessMemory(hProcess, base + nt.OptionalHeader.AddressOfEntryPoint, &trampolineTemplate[TrampolineTemplateOriginalEntryPointOffset], 16, nullptr))
					throw Utils::Win32::Error("ReadProcessMemory3");

				for (size_t i = 0; i < 16; i++)
					log << std::format(L"{:02x} ", trampolineTemplate[TrampolineTemplateOriginalEntryPointOffset + i]);
				log << L"\n";

				log << std::format(L"EntryPoint: {:16x}\n", reinterpret_cast<size_t>(base + nt.OptionalHeader.AddressOfEntryPoint));
				*reinterpret_cast<void**>(&trampolineTemplate[TrampolineTemplateEntryImplFromDllOffset]) = InjectEntryPoint;
				log << std::format(L"InjectEntryPoint: {:16x}\n", reinterpret_cast<size_t>(InjectEntryPoint));
				*reinterpret_cast<void**>(&trampolineTemplate[TrampolineTemplateLoadLibraryWOffset]) = LoadLibraryW;
				log << std::format(L"LoadLibraryW: {:16x}\n", reinterpret_cast<size_t>(LoadLibraryW));
				const auto dllPath = Utils::Win32::Modules::PathFromModule(g_hInstance).wstring();
				const auto dllPathByteBuffer = std::span(reinterpret_cast<const uint8_t*>(dllPath.c_str()), dllPath.size() * 2);
				trampolineTemplate.insert(trampolineTemplate.end(), dllPathByteBuffer.begin(), dllPathByteBuffer.end());
				trampolineTemplate.resize(trampolineTemplate.size() + 16, 0);  // null terminate

				const auto preferredAddress = static_cast<char*>(regions.back().BaseAddress) + regions.back().RegionSize;
				void* trampolineAddress = preferredAddress;
				for (size_t i = 0; i < 0x10000000; i += 0x10000)
					if ((trampolineAddress = VirtualAllocEx(hProcess, preferredAddress + i, trampolineTemplate.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)))
						break;
				if (!trampolineAddress)
					throw Utils::Win32::Error("VirtualAllocEx");
				if (!WriteProcessMemory(hProcess, trampolineAddress, &trampolineTemplate[0], trampolineTemplate.size(), nullptr))
					throw Utils::Win32::Error("WriteProcessMemory");

				std::vector entryThunk(EntryPointThunkTemplate, EntryPointThunkTemplate + sizeof EntryPointThunkTemplate);
				*reinterpret_cast<void**>(&entryThunk[0x08]) = trampolineAddress;
				log << std::format(L"trampolineAddress: {:16x}\n", reinterpret_cast<size_t>(trampolineAddress));

				DWORD dummy;
				if (!VirtualProtectEx(hProcess, base + nt.OptionalHeader.AddressOfEntryPoint, 16, PAGE_EXECUTE_READWRITE, &dummy))
					throw Utils::Win32::Error("VirtualProtectEx");
				if (!WriteProcessMemory(hProcess, base + nt.OptionalHeader.AddressOfEntryPoint, &entryThunk[0], entryThunk.size(), nullptr))
					throw Utils::Win32::Error("WriteProcessMemory");
			}
		} catch (std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
				L"Failed to load XivAlexander into child process: {}\n\n"
				L"Process ID: {}",
				e.what(), lpProcessInformation->dwProcessId);
		}
		
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
	auto filename = Utils::Win32::Modules::PathFromModule().filename().wstring();
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

extern "C" __declspec(dllexport) void __stdcall InjectEntryPoint(void* originalEntryPoint, void* thunkAddress) {
	DWORD dummy;
	FlushInstructionCache(GetCurrentProcess(), originalEntryPoint, 16);
	VirtualProtect(originalEntryPoint, 16, PAGE_EXECUTE_READ, &dummy);
	VirtualFree(thunkAddress, 0, MEM_RELEASE);
	
	// not going to allocate stack or something yet, so must use the minimum; run stuff on thread
	const auto h = CreateThread(nullptr, 0, [](void*) -> DWORD {
		RunBeforeAppInit();
		FreeLibraryAndExitThread(g_hInstance, 0);
	}, nullptr, 0, nullptr);
	assert(h);
	WaitForSingleObject(h, INFINITE);
	CloseHandle(h);
}
