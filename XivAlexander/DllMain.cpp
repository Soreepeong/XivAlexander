#include "pch.h"
#include "App_App.h"
#include "App_Misc_Hooks.h"
#include "App_Misc_FreeGameMutex.h"

HINSTANCE g_hInstance;

static DWORD WINAPI XivAlexanderThreadBody(void*) {
	{
		App::Misc::Logger logger;

		Utils::Win32::SetThreadDescription(GetCurrentThread(), L"XivAlexander::XivAlexanderThread");
		
		try {
			App::Misc::FreeGameMutex::FreeGameMutex();
		} catch (std::exception& e) {
			App::Misc::Logger::GetLogger().Format<App::LogLevel::Warning>(App::LogCategory::General, "Failed to free game mutex: {}", e.what());
		}
		
		try {
			App::App app;
			logger.Log(App::LogCategory::General, u8"XivAlexander initialized.");
			app.Run();
		} catch (const std::exception& e) {
			if (e.what())
				logger.Format<App::LogLevel::Error>(App::LogCategory::General, u8"Error: {}", e.what());
		}
	}
	FreeLibraryAndExitThread(g_hInstance, 0);
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
	g_hInstance = hInstance;
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
			MH_Initialize();
			for (const auto& signature : App::Signatures::AllSignatures())
				signature->Setup();
			break;
		case DLL_PROCESS_DETACH:
			MH_Uninitialize();
			break;
	}
	return TRUE;
}

extern "C" __declspec(dllexport) int __stdcall LoadXivAlexander(void* lpReserved) {
	if (App::App::Instance())
		return 0;
	auto bLibraryLoaded = false;
	try {
		LoadLibraryW(Utils::Win32::Modules::PathFromModule(g_hInstance).c_str());
		bLibraryLoaded = true;
		
		Utils::Win32::Closeable::Handle hThread(CreateThread(nullptr, 0, XivAlexanderThreadBody, nullptr, 0, nullptr),
			Utils::Win32::Closeable::Handle::Null,
			"LoadXivAlexander/CreateThread");
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(std::format("LoadXivAlexander error: {}\n", e.what()).c_str());
		const auto lastError = GetLastError();
		if (bLibraryLoaded)
			FreeLibrary(g_hInstance);
		return lastError;
	}
}

extern "C" __declspec(dllexport) int __stdcall DisableUnloading(size_t bDisable) {
	App::App::SetDisableUnloading(bDisable);
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall UnloadXivAlexander(void* lpReserved) {
	if (auto pApp = App::App::Instance()) {
		try {
			pApp->Unload();
		} catch (std::exception& e) {
			Utils::Win32::MessageBoxF(nullptr, MB_ICONERROR, L"XivAlexander", L"Unable to unload XivAlexander: {}", Utils::FromUtf8(e.what()));
		}
		return 0;
	}
	return -1;
}

extern "C" __declspec(dllexport) int __stdcall ReloadConfiguration(void* lpReserved) {
	if (App::App::Instance()) {
		App::Config::Instance().Runtime.Reload(true);
		App::Config::Instance().Game.Reload(true);
	}
	return 0;
}