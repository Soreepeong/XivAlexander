#include "pch.h"
#include "App_DalamudHandlerApp.h"

#include "App_ConfigRepository.h"
#include "App_Misc_Hooks.h"
#include "App_Misc_Logger.h"
#include "App_XivAlexApp.h"
#include "DllMain.h"

// https://docs.microsoft.com/en-us/windows/win32/devnotes/ldrdllnotification

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
	ULONG Flags;                    //Reserved.
	PCUNICODE_STRING FullDllName;   //The full path name of the DLL module.
	PCUNICODE_STRING BaseDllName;   //The base file name of the DLL module.
	PVOID DllBase;                  //A pointer to the base address for the DLL in memory.
	ULONG SizeOfImage;              //The size of the DLL image, in bytes.
} LDR_DLL_LOADED_NOTIFICATION_DATA, * PLDR_DLL_LOADED_NOTIFICATION_DATA;

typedef struct _LDR_DLL_UNLOADED_NOTIFICATION_DATA {
	ULONG Flags;                    //Reserved.
	PCUNICODE_STRING FullDllName;   //The full path name of the DLL module.
	PCUNICODE_STRING BaseDllName;   //The base file name of the DLL module.
	PVOID DllBase;                  //A pointer to the base address for the DLL in memory.
	ULONG SizeOfImage;              //The size of the DLL image, in bytes.
} LDR_DLL_UNLOADED_NOTIFICATION_DATA, * PLDR_DLL_UNLOADED_NOTIFICATION_DATA;

typedef union _LDR_DLL_NOTIFICATION_DATA {
	LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
	LDR_DLL_UNLOADED_NOTIFICATION_DATA Unloaded;
} LDR_DLL_NOTIFICATION_DATA, * PLDR_DLL_NOTIFICATION_DATA, * const PCLDR_DLL_NOTIFICATION_DATA;

typedef VOID(CALLBACK* PLDR_DLL_NOTIFICATION_FUNCTION)(
	_In_     ULONG                       NotificationReason,
	_In_     PCLDR_DLL_NOTIFICATION_DATA NotificationData,
	_In_opt_ PVOID                       Context
	);

typedef NTSTATUS(NTAPI* LdrRegisterDllNotificationType) (
	_In_     ULONG                          Flags,
	_In_     PLDR_DLL_NOTIFICATION_FUNCTION NotificationFunction,
	_In_opt_ PVOID                          Context,
	_Out_    PVOID* Cookie
	);

typedef NTSTATUS(NTAPI* LdrUnregisterDllNotificationType) (
	_In_     PVOID* Cookie
	);

constexpr static auto LDR_DLL_NOTIFICATION_REASON_LOADED = 1UL;
constexpr static auto LDR_DLL_NOTIFICATION_REASON_UNLOADED = 2UL;

struct App::DalamudHandlerApp::Implementation {
	const std::shared_ptr<App::Config> Config;
	const std::shared_ptr<App::Misc::Logger> Logger;
	const Utils::Win32::LoadedModule NtDll;
	const LdrRegisterDllNotificationType LdrRegisterDllNotification;
	const LdrUnregisterDllNotificationType LdrUnregisterDllNotification;

	std::vector<std::unique_ptr<Misc::Hooks::PointerFunction<DWORD, LPVOID>>> InitializeHooks;
	bool InitializeCalled = false;

	Utils::CallOnDestruction::Multiple Cleanup;

	Implementation()
		: Config(App::Config::Acquire())
		, Logger(App::Misc::Logger::Acquire())
		, NtDll(L"ntdll.dll")
		, LdrRegisterDllNotification(NtDll.GetProcAddress<LdrRegisterDllNotificationType>("LdrRegisterDllNotification"))
		, LdrUnregisterDllNotification(NtDll.GetProcAddress<LdrUnregisterDllNotificationType>("LdrUnregisterDllNotification")) {


		if (PVOID cookie{}; LdrRegisterDllNotification(0, [](ULONG notificationReason, PCLDR_DLL_NOTIFICATION_DATA notificationData, PVOID context) {
			reinterpret_cast<Implementation*>(context)->OnDllNotification(notificationReason, notificationData);
			}, this, &cookie) == 0) {
			Cleanup += [this, cookie]() mutable { LdrUnregisterDllNotification(&cookie); };
		}
	}

	~Implementation() {
		Cleanup.Clear();
	}

	void OnDllNotification(ULONG notificationReason, PCLDR_DLL_NOTIFICATION_DATA notificationData) {
		if (notificationReason != LDR_DLL_NOTIFICATION_REASON_LOADED)
			return;
		if (lstrcmpiW(notificationData->Loaded.BaseDllName->Buffer, L"Dalamud.Boot.dll") != 0)
			return;

		Utils::Win32::LoadedModule dll(static_cast<HMODULE>(notificationData->Loaded.DllBase), false);
		const auto Initialize = dll.GetProcAddress<LPTHREAD_START_ROUTINE>("Initialize");
		if (!Initialize)
			return;

		InitializeHooks.emplace_back(std::make_unique<Misc::Hooks::PointerFunction<DWORD, LPVOID>>("Dalamud.Boot!Initialize", Initialize));

		// Intentional leak; neither of this DLL or Dalamud Boot DLL will ever get unloaded
		// Dll unload callback is called after the DLL is gone; MH will try to revert the unloaded region of memory
		new Utils::CallOnDestruction(InitializeHooks.back()->SetHook([this, pHook = InitializeHooks.back().get()](LPVOID lpParam) -> DWORD {
			if (InitializeCalled) {
				Logger->Format(LogCategory::General, "Dropping subsequent Dalamud load request");
				return 0;
			}

			InitializeCalled = true;
			auto pXivAlexApp = App::XivAlexApp::GetCurrentApp();
			if (!pXivAlexApp) {
				Logger->Format(LogCategory::General, "Dalamud Initialize called; waiting for game window to become available");
				const auto loadedEvent = Utils::Win32::Event::Create();
				const auto appCreatedCallback = App::XivAlexApp::OnAppCreated([&](auto& app) { pXivAlexApp = &app; loadedEvent.Set(); });
				if (!(pXivAlexApp = App::XivAlexApp::GetCurrentApp()))
					loadedEvent.Wait();
			}

			pXivAlexApp->RunOnGameLoop([&]() { /* empty */ });

			Logger->Format(LogCategory::General, "Calling Dalamud Initialize");
			const auto res = pHook->bridge(lpParam);
			Logger->Format(LogCategory::General, "Dalamud Initialize result: {}", res);
			if (res)
				InitializeCalled = false;
			return res;
		}));
	}
};

App::DalamudHandlerApp::DalamudHandlerApp()
	: m_module(Utils::Win32::LoadedModule::LoadMore(Dll::Module()))
	, m_detectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
	, m_pImpl(std::make_unique<Implementation>()) {
}

App::DalamudHandlerApp::~DalamudHandlerApp() = default;

static std::unique_ptr<App::DalamudHandlerApp> s_dalamudHandlerApp;

void App::DalamudHandlerApp::LoadDalamudHandler() {
	if (s_dalamudHandlerApp)
		return;
	
	s_dalamudHandlerApp = std::make_unique<App::DalamudHandlerApp>();
}
