#include "pch.h"
#include "App_InjectOnCreateProcessApp.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"

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
			const auto result = CreateProcessW.bridge(
				lpApplicationName,
				lpCommandLine,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags,
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo,
				lpProcessInformation);
			if (result)
				PostProcessExecution(lpCommandLine ? lpCommandLine : lpApplicationName, dwCreationFlags, lpProcessInformation);
			return result;
			});
		m_cleanup += CreateProcessA.SetHook([this](_In_opt_ LPCSTR lpApplicationName, _Inout_opt_ LPSTR lpCommandLine, _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes, _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes, _In_ BOOL bInheritHandles, _In_ DWORD dwCreationFlags, _In_opt_ LPVOID lpEnvironment, _In_opt_ LPCSTR lpCurrentDirectory, _In_ LPSTARTUPINFOA lpStartupInfo, _Out_ LPPROCESS_INFORMATION lpProcessInformation) -> BOOL {
			const auto result = CreateProcessA.bridge(
				lpApplicationName,
				lpCommandLine,
				lpProcessAttributes,
				lpThreadAttributes,
				bInheritHandles,
				dwCreationFlags,
				lpEnvironment,
				lpCurrentDirectory,
				lpStartupInfo,
				lpProcessInformation);
			if (result)
				PostProcessExecution(Utils::FromOem(lpCommandLine ? lpCommandLine : lpApplicationName), dwCreationFlags, lpProcessInformation);
			return result;
			});
	}

	~Implementation() = default;

	void PostProcessExecution(const std::wstring& commandLine, DWORD dwCreationFlags, LPPROCESS_INFORMATION lpProcessInformation) {
		if (dwCreationFlags & (CREATE_SUSPENDED | DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS))
			return;

		try {
			int argc = 0;
			LPWSTR* argv = CommandLineToArgvW(&commandLine[0], &argc);
			if (!argv)
				throw Utils::Win32::Error("CommandLineToArgvW");
			const auto cleanup = Utils::CallOnDestruction([argv]() { LocalFree(argv); });

			const auto gamePath = Utils::Win32::Modules::PathFromModule().parent_path().parent_path() / L"game" / L"ffxiv_dx11.exe";
			const auto currentPath = std::filesystem::path(argv[0]);
			if (!equivalent(gamePath, currentPath))
				return;

			if (WaitForInputIdle(lpProcessInformation->hProcess, 10000) == WAIT_TIMEOUT)
				throw std::runtime_error("Timed out waiting for the game to run.");

			HWND hwnd = nullptr;
			while (WaitForSingleObject(lpProcessInformation->hProcess, 100) == WAIT_TIMEOUT) {
				hwnd = nullptr;
				while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
					DWORD pid;
					GetWindowThreadProcessId(hwnd, &pid);
					if (pid == lpProcessInformation->dwProcessId)
						break;
				}
				if (hwnd && IsWindowVisible(hwnd))
					break;
			}
			if (!hwnd)
				throw std::runtime_error("Game process exited before initialization.");

			const auto hModule = Utils::Win32::Modules::InjectedModule(lpProcessInformation->hProcess, Utils::Win32::Modules::PathFromModule(g_hInstance));
			if (const auto loadResult = hModule.Call("EnableXivAlexander", reinterpret_cast<void*>(1), "EnableXivAlexander(1)"); loadResult != 0)
				throw std::runtime_error(std::format("Failed to start the addon: exit code {}", loadResult));

		} catch (std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, L"XivAlexander",
				L"Failed to load XivAlexander into the game process: {}\n\n"
				L"Process ID: {}",
				e.what(), lpProcessInformation->dwProcessId);
		}
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
