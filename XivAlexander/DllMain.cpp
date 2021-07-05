#include "pch.h"
#include "DllMain.h"
#include "resource.h"
#include "XivAlexander/XivAlexander.h"

static Utils::Win32::LoadedModule s_hModule;
static Utils::Win32::ActivationContext s_hActivationContext;

const Utils::Win32::LoadedModule& Dll::Module() {
	return s_hModule;
}

const Utils::Win32::ActivationContext& Dll::ActivationContext() {
	return s_hActivationContext;
}

XIVALEXANDER_DLLEXPORT DWORD XivAlexDll::LaunchXivAlexLoaderWithStdinHandle(const std::vector<HANDLE>& hSources, const wchar_t* mode, bool wait, const wchar_t* loaderName) {
	const auto companion = loaderName ? loaderName : Dll::Module().PathOf().parent_path() / XivAlex::XivAlexLoaderNameW;
	if (!exists(companion))
		throw std::runtime_error("loader not found");
	
	Utils::Win32::Handle companionProcess;
	{
		std::vector<Utils::Win32::Handle> hInheritableTargetProcessHandles;
		for (const auto hSource : hSources) {
			if (auto h = INVALID_HANDLE_VALUE;
				!DuplicateHandle(GetCurrentProcess(), hSource, GetCurrentProcess(), &h, 0, TRUE, DUPLICATE_SAME_ACCESS))
				throw Utils::Win32::Error("DuplicateHandle(hProcess)");
			else
				hInheritableTargetProcessHandles.emplace_back(h, INVALID_HANDLE_VALUE, "DuplicateHandle(hProcess.2)");
		}

		Utils::Win32::Handle hStdinRead, hStdinWrite;
		if (auto r = INVALID_HANDLE_VALUE, w = INVALID_HANDLE_VALUE;
			!CreatePipe(&r, &w, nullptr, 0))
			throw Utils::Win32::Error("CreatePipe");
		else {
			hStdinRead.Attach(r, INVALID_HANDLE_VALUE, true, "CreatePipe(Read)");
			hStdinWrite.Attach(w, INVALID_HANDLE_VALUE, true, "CreatePipe(Write)");
		}

		Utils::Win32::Handle hInheritableStdinRead;
		if (auto h = INVALID_HANDLE_VALUE;
			!DuplicateHandle(GetCurrentProcess(), hStdinRead, GetCurrentProcess(), &h, 0, TRUE, DUPLICATE_SAME_ACCESS))
			throw Utils::Win32::Error("DuplicateHandle(hStdinRead)");
		else
			hInheritableStdinRead.Attach(h, INVALID_HANDLE_VALUE, true, "DuplicateHandle(hStdinRead.2)");

		{
			STARTUPINFOW si{};
			PROCESS_INFORMATION pi{};

			si.cb = sizeof si;
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = hInheritableStdinRead;
			si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

			auto args = std::format(L"\"{}\" --action {}", companion, mode);
			if (!CreateProcessW(companion.c_str(), &args[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
				throw Utils::Win32::Error("CreateProcess");

			assert(pi.hProcess);
			assert(pi.hThread);

			companionProcess = Utils::Win32::Handle(pi.hProcess, true);
			CloseHandle(pi.hThread);
		}

		for (const auto& hInheritableTargetProcessHandle : hInheritableTargetProcessHandles) {
			const auto handleNumber = static_cast<uint64_t>(reinterpret_cast<size_t>(static_cast<HANDLE>(hInheritableTargetProcessHandle)));
			static_assert(sizeof handleNumber == 8);
			DWORD written;
			if (!WriteFile(hStdinWrite, &handleNumber, sizeof handleNumber, &written, nullptr) || written != sizeof handleNumber)
				throw Utils::Win32::Error("WriteFile");
		}
	}

	DWORD retCode = 0;

	if (wait) {
		companionProcess.Wait();
		if (!GetExitCodeProcess(companionProcess, &retCode))
			throw Utils::Win32::Error("GetExitCodeProcess");
	}

	return retCode;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {	
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
		{
			try {
				s_hModule.Attach(hInstance, Utils::Win32::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
				s_hActivationContext = Utils::Win32::ActivationContext(ACTCTXW {
					.cbSize = sizeof ACTCTXW,
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

		case DLL_PROCESS_DETACH:
		{
			auto fail = false;
			if (const auto res = MH_Uninitialize(); res != MH_OK) {
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

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::DisableAllApps(void*) {
	EnableXivAlexander(0);
	EnableInjectOnCreateProcess(0);
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::CallFreeLibrary(void*) {
	FreeLibraryAndExitThread(Dll::Module(), 0);
}
