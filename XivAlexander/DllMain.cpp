#include "pch.h"

#include "resource.h"
#include "XivAlexander/XivAlexander.h"

Utils::Win32::Closeable::LoadedModule g_hInstance;
Utils::Win32::Closeable::ActivationContext g_hActivationContext;

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {	
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
		{
			try {
				g_hInstance.Attach(hInstance, Utils::Win32::Closeable::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
				g_hActivationContext = Utils::Win32::Closeable::ActivationContext(ACTCTXW {
					.cbSize = sizeof ACTCTXW,
					.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
					.lpResourceName = MAKEINTRESOURCE(IDR_RT_MANIFEST_LATE_ACTIVATION),
					.hModule = g_hInstance,
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
	FreeLibraryAndExitThread(g_hInstance, 0);
}
