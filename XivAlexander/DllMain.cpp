#include "pch.h"

#include "App_XivAlexApp.h"
#include "App_Misc_Hooks.h"

HINSTANCE g_hInstance;

class DebuggerPresenceDisabler {
	App::Misc::Hooks::PointerFunction<BOOL> m_IsDebuggerPresent;
	Utils::CallOnDestruction m_disableDebuggerPresence;

public:
	DebuggerPresenceDisabler()
		: m_IsDebuggerPresent("DebuggerPresenceDisabler::m_IsDebuggerPresent", ::IsDebuggerPresent)
		, m_disableDebuggerPresence(m_IsDebuggerPresent.SetHook([]() {return FALSE; })) {
	}
};

static std::unique_ptr<App::XivAlexApp> s_app;
static std::unique_ptr<DebuggerPresenceDisabler> s_debuggerPresenceDisabler;

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
	g_hInstance = hInstance;
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
			MH_Initialize();
			break;

		case DLL_PROCESS_DETACH:
			MH_Uninitialize();
			break;
	}
	return TRUE;
}

extern "C" __declspec(dllexport) int __stdcall EnableXivAlexander(size_t bEnable) {
	try {
		s_app = bEnable ? std::make_unique<App::XivAlexApp>() : nullptr;
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(std::format("LoadXivAlexander error: {}\n", e.what()).c_str());
		return -1;
	}
}

extern "C" __declspec(dllexport) int __stdcall EnableDebuggerPresenceDisabler(size_t bEnable) {
	try {
		s_debuggerPresenceDisabler = bEnable ? std::make_unique<DebuggerPresenceDisabler>() : nullptr;
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(std::format("EnableDebuggerPresenceDisabler error: {}\n", e.what()).c_str());
		return -1;
	}
}

extern "C" __declspec(dllexport) int __stdcall ReloadConfiguration(void* lpReserved) {
	if (s_app) {
		App::Config::Instance().Runtime.Reload(true);
		App::Config::Instance().Game.Reload(true);
	}
	return 0;
}
