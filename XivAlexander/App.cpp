#include "pch.h"
#include "App_Network_SocketHook.h"
#include "App_Misc_FreeGameMutex.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Feature_EffectApplicationDelayLogger.h"
#include "App_Window_Log.h"
#include "App_Window_Main.h"
#include "App_Window_Config.h"

namespace App {
	class App;
}

HINSTANCE g_hInstance;

static bool s_bFreeLibraryAndExitThread = true;
static bool s_bUnloadDisabled = false;

static HWND FindGameMainWindow() {
	HWND hwnd = 0;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid != GetCurrentProcessId())
			continue;
		return hwnd;
	}
	return 0;
}

class App::App {
	static App* m_instance;
	
public:
	const HWND m_hGameMainWindow;
	std::vector<Utils::CallOnDestruction> m_cleanupPendingDestructions;

	std::mutex m_runInMessageLoopLock;
	std::queue<std::function<void()>> m_qRunInMessageLoop;

	std::unique_ptr<::App::Network::SocketHook> m_socketHook;
	std::unique_ptr<::App::Feature::AnimationLockLatencyHandler> m_animationLockLatencyHandler;
	std::unique_ptr<::App::Feature::IpcTypeFinder> m_ipcTypeFinder;
	std::unique_ptr<::App::Feature::AllIpcMessageLogger> m_allIpcMessageLogger;
	std::unique_ptr<::App::Feature::EffectApplicationDelayLogger> m_effectApplicationDelayLogger;

	std::unique_ptr<Window::Log> m_logWindow;
	std::unique_ptr<Window::Main> m_trayWindow;

	DWORD m_mainThreadId = -1;
	int m_nWndProcDepth = 0;

	WNDPROC m_originalGameMainWndProc = nullptr;

	LRESULT CALLBACK OverridenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		m_mainThreadId = GetCurrentThreadId();
		if (m_nWndProcDepth == 0) {
			while (!m_qRunInMessageLoop.empty()) {
				std::function<void()> fn;
				{
					std::lock_guard _lock(m_runInMessageLoopLock);
					fn = std::move(m_qRunInMessageLoop.front());
					m_qRunInMessageLoop.pop();
				}
				fn();
			}
		}
		
		switch (msg) {
			case WM_DESTROY:
				SendMessage(m_trayWindow->GetHandle(), WM_CLOSE, 0, 1);
				break;
		}

		m_nWndProcDepth += 1;
		auto res = CallWindowProcW(m_originalGameMainWndProc, hwnd, msg, wParam, lParam);
		m_nWndProcDepth -= 1;
		return res;
	}

	const LONG_PTR m_overridenGameMainWndProc = reinterpret_cast<LONG_PTR>(
		static_cast<WNDPROC>([](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return m_instance->OverridenWndProc(hwnd, msg, wParam, lParam); })
		);

	void OnCleanup(std::function<void()> cb) {
		m_cleanupPendingDestructions.push_back(Utils::CallOnDestruction(cb));
	}

	void OnCleanup(Utils::CallOnDestruction cb) {
		m_cleanupPendingDestructions.push_back(std::move(cb));
	}

	void SetupTrayWindow() {
		m_trayWindow = std::make_unique<Window::Main>(m_hGameMainWindow, [this]() {
			try {
				this->Unload();
			} catch(std::exception& e) {
				SetupTrayWindow();
				MessageBoxW(m_trayWindow->GetHandle(), Utils::FromUtf8(Utils::FormatString("Unable to unload XivAlexander: %s", e.what())).c_str(), L"XivAlexander", MB_ICONERROR);
			}
			});
	}

	App()
		: m_hGameMainWindow(FindGameMainWindow()) {
		if (m_instance)
			throw std::runtime_error("App already initialized");
		
		m_instance = this;

		try {
			Misc::FreeGameMutex::FreeGameMutex();
		} catch (std::exception& e) {
			Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::General, "Failed to free game mutex: %s", e.what());
		}

		try {
			Config::Instance();
			OnCleanup([]() { Config::DestroyInstance(); });

			Scintilla_RegisterClasses(g_hInstance);
			OnCleanup([]() { Scintilla_ReleaseResources(); });

			if (!m_hGameMainWindow)
				throw std::exception("Game main window not found!");

			MH_Initialize();
			OnCleanup([this]() { MH_Uninitialize(); });
			for (const auto& signature : Signatures::AllSignatures())
				signature->Setup();

			m_originalGameMainWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hGameMainWindow, GWLP_WNDPROC, m_overridenGameMainWndProc));
			OnCleanup([this]() {
				QueueRunOnMessageLoop([]() {
					for (const auto& signature : Signatures::AllSignatures())
						signature->Cleanup();
					MH_DisableHook(MH_ALL_HOOKS);
					}, true);
				SetWindowLongPtrW(m_hGameMainWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalGameMainWndProc));
				});

			m_socketHook = std::make_unique<Network::SocketHook>(m_hGameMainWindow);
			OnCleanup([this]() { m_socketHook = nullptr; });

			SetupTrayWindow();
			OnCleanup([this]() { m_trayWindow = nullptr; });

			auto& config = Config::Instance().Runtime;

			if (config.AlwaysOnTop)
				SetWindowPos(m_hGameMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			else
				SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			OnCleanup(config.AlwaysOnTop.OnChangeListener([&](Config::ItemBase&) {
				if (config.AlwaysOnTop)
					SetWindowPos(m_hGameMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				else
					SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				}));
			OnCleanup([this]() { SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); });

			if (config.UseHighLatencyMitigation)
				m_animationLockLatencyHandler = std::make_unique<Feature::AnimationLockLatencyHandler>();
			OnCleanup(config.UseHighLatencyMitigation.OnChangeListener([&](Config::ItemBase&) {
				if (config.UseHighLatencyMitigation)
					m_animationLockLatencyHandler = std::make_unique<Feature::AnimationLockLatencyHandler>();
				else
					m_animationLockLatencyHandler = nullptr;
				}));
			OnCleanup([this]() { m_animationLockLatencyHandler = nullptr; });

			if (config.UseOpcodeFinder)
				m_ipcTypeFinder = std::make_unique<Feature::IpcTypeFinder>();
			OnCleanup(config.UseOpcodeFinder.OnChangeListener([&](Config::ItemBase&) {
				if (config.UseOpcodeFinder)
					m_ipcTypeFinder = std::make_unique<Feature::IpcTypeFinder>();
				else
					m_ipcTypeFinder = nullptr;
				}));
			OnCleanup([this]() { m_ipcTypeFinder = nullptr; });

			if (config.UseAllIpcMessageLogger)
				m_allIpcMessageLogger = std::make_unique<Feature::AllIpcMessageLogger>();
			OnCleanup(config.UseAllIpcMessageLogger.OnChangeListener([&](Config::ItemBase&) {
				if (config.UseAllIpcMessageLogger)
					m_allIpcMessageLogger = std::make_unique<Feature::AllIpcMessageLogger>();
				else
					m_allIpcMessageLogger = nullptr;
				}));
			OnCleanup([this]() { m_allIpcMessageLogger = nullptr; });

			if (config.UseEffectApplicationDelayLogger)
				m_effectApplicationDelayLogger = std::make_unique<Feature::EffectApplicationDelayLogger>();
			OnCleanup(config.UseEffectApplicationDelayLogger.OnChangeListener([&](Config::ItemBase&) {
				if (config.UseEffectApplicationDelayLogger)
					m_effectApplicationDelayLogger = std::make_unique<Feature::EffectApplicationDelayLogger>();
				else
					m_effectApplicationDelayLogger = nullptr;
				}));
			OnCleanup([this]() { m_effectApplicationDelayLogger = nullptr; });

			if (config.ShowLoggingWindow)
				m_logWindow = std::make_unique<Window::Log>();
			OnCleanup(config.ShowLoggingWindow.OnChangeListener([&](Config::ItemBase&) {
				if (config.ShowLoggingWindow)
					m_logWindow = std::make_unique<Window::Log>();
				else
					m_logWindow = nullptr;
				}));
			OnCleanup([this]() { m_logWindow = nullptr; });

		} catch (std::exception&) {
			Config::Instance().SetQuitting();
			while (!m_cleanupPendingDestructions.empty())
				m_cleanupPendingDestructions.pop_back();

			m_instance = nullptr;
			throw;
		}
	}

	~App() {
		Config::Instance().SetQuitting();
		while (!m_cleanupPendingDestructions.empty())
			m_cleanupPendingDestructions.pop_back();

		m_instance = nullptr;
	}

	void Run() {
		QueueRunOnMessageLoop([&]() {
			for (const auto& signature : Signatures::AllSignatures())
				signature->Startup();
		}, true);

		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0)) {
			bool dispatchMessage = true;
			for (const auto pWindow : Window::Base::GetAllOpenWindows()) {
				const auto hWnd = pWindow->GetHandle();
				const auto hAccel = pWindow->GetAcceleratorTable();
				if (hAccel && (hWnd == msg.hwnd || IsChild(hWnd, msg.hwnd))){
					if (TranslateAcceleratorW(hWnd, hAccel, &msg)) {
						dispatchMessage = false;
						break;
					}
					if (IsDialogMessageW(hWnd, &msg)) {
						dispatchMessage = false;
						break;
					}
				}
			}
			if (dispatchMessage) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}
		}
	}

	void QueueRunOnMessageLoop(std::function<void()> f, bool wait = false) {
		if (!wait) {
			{
				std::lock_guard _lock(m_runInMessageLoopLock);
				m_qRunInMessageLoop.push(f);
			}
			PostMessageW(m_hGameMainWindow, WM_NULL, 0, 0);

		} else {
			Utils::Win32Handle hEvent(CreateEventW(nullptr, true, false, nullptr),
				INVALID_HANDLE_VALUE, 
				"App::QueueRunOnMessageLoop/CreateEventW");
			const auto fn = [&f, &hEvent]() {
				try {
					f();
				} catch (...) {

				}
				SetEvent(hEvent);
			};
			{
				std::lock_guard _lock(m_runInMessageLoopLock);
				m_qRunInMessageLoop.push(fn);
			}
			SendMessageW(m_hGameMainWindow, WM_NULL, 0, 0);
			WaitForSingleObject(hEvent, INFINITE);
		}
	}

	int Unload() {
		if (s_bUnloadDisabled)
			throw std::exception("Unloading is currently disabled.");

		if (GetWindowLongPtrW(m_hGameMainWindow, GWLP_WNDPROC) != m_overridenGameMainWndProc)
			throw std::exception("Something has hooked the game process after XivAlexander, so you cannot unload XivAlexander until that other thing has been unloaded.");

		if (m_trayWindow)
			SendMessage(m_trayWindow->GetHandle(), WM_CLOSE, 0, 1);
		return 0;
	}

	static
	App* GetInstance() {
		return m_instance;
	}
};

App::App* App::App::m_instance;

static DWORD WINAPI DllThread(PVOID param1) {
	Utils::SetThreadDescription(GetCurrentThread(), L"XivAlexander::DllThread");
	{
		App::Misc::Logger logger;
		try {
			App::App app;
			logger.Log(App::LogCategory::General, u8"XivAlexander initialized.");
			app.Run();
		} catch (const std::exception& e) {
			if (e.what())
				logger.Format<App::LogLevel::Error>(App::LogCategory::General, u8"Error: %s", e.what());
		}
	}
	if (s_bFreeLibraryAndExitThread)
		FreeLibraryAndExitThread(g_hInstance, 0);
	return 0;
}

BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
) {
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
			g_hInstance = hinstDLL;
			break;
	}
	return TRUE;
}

extern "C" __declspec(dllexport) int __stdcall LoadXivAlexander(void* lpReserved) {
	if (App::App::GetInstance())
		return 0;
	try {
		Utils::Win32Handle hThread(CreateThread(nullptr, 0, DllThread, g_hInstance, 0, nullptr),
			Utils::NullHandle,
			"LoadXivAlexander/CreateThread");
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(Utils::FormatString("LoadXivAlexander error: %s\n", e.what()).c_str());
		return GetLastError();
	}
}

extern "C" __declspec(dllexport) int __stdcall DisableUnloading(size_t bDisable) {
	s_bUnloadDisabled = !!bDisable;
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall SetFreeLibraryAndExitThread(size_t use) {
	s_bFreeLibraryAndExitThread = !!use;
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall UnloadXivAlexander(void* lpReserved) {
	if (auto pApp = App::App::GetInstance()) {
		try {
			pApp->Unload();
		} catch (std::exception& e) {
			MessageBoxW(nullptr, Utils::FromUtf8(Utils::FormatString("Unable to unload XivAlexander: %s", e.what())).c_str(), L"XivAlexander", MB_ICONERROR);
		}
		return 0;
	}
	return -1;
}

extern "C" __declspec(dllexport) int __stdcall ReloadConfiguration(void* lpReserved) {
	if (App::App::GetInstance()) {
		App::Config::Instance().Runtime.Reload(true);
		App::Config::Instance().Game.Reload(true);
	}
	return 0;
}