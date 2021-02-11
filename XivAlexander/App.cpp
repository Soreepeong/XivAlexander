#include "pch.h"
#include "App.h"
#include "App_Network_SocketHook.h"
#include "App_Misc_FreeGameMutex.h"
#include <conio.h>
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Window_Log.h"
#include "App_Window_TrayIcon.h"

HINSTANCE g_hInstance;

static HWND FindGameMainWindow() {
	HWND hwnd = 0;
	while (hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr)) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid != GetCurrentProcessId())
			continue;
		return hwnd;
	}
	return 0;

	EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
		DWORD pid;
		HWND& hGameMainWindow = *reinterpret_cast<HWND*>(lParam);
		std::wstring buf;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid != GetCurrentProcessId())
			return TRUE;

		buf.reserve(128);
		buf.resize(GetClassNameW(hwnd, &buf[0], static_cast<int>(buf.capacity())));
		if (buf != L"FFXIVGAME")
			return TRUE;

		hGameMainWindow = hwnd;
		return FALSE;
		}, reinterpret_cast<LPARAM>(&hwnd));
	return hwnd;
}

static std::unique_ptr<App::App> pInstance;

class App::App::Internals {

public:
	const HWND m_hGameMainWindow;
	std::vector<std::function<void()>> m_initFailureCleanupList;

	std::mutex m_runInMessageLoopLock;
	std::queue<std::function<void()>> m_qRunInMessageLoop;

	std::unique_ptr<::App::Network::SocketHook> m_socketHook;
	std::unique_ptr<::App::Feature::AnimationLockLatencyHandler> m_animationLockLatencyHandler;
	std::unique_ptr<::App::Feature::IpcTypeFinder> m_ipcTypeFinder;
	Utils::Win32Handle<> m_hUnloadEvent;
	bool m_bUnloadDisabled = false;

	std::unique_ptr<Window::Log> m_logWindow;
	std::unique_ptr<Window::TrayIcon> m_trayWindow;

	DWORD m_mainThreadId = -1;
	int m_nWndProcDepth = 0;
	
	WNDPROC m_originalGameMainWndProc = nullptr;
	LRESULT CALLBACK OverridenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		m_mainThreadId = GetCurrentThreadId();
		if (m_nWndProcDepth == 0) {
			while (!m_qRunInMessageLoop.empty()) {
				std::function<void()> fn;
				{
					std::lock_guard<std::mutex> _lock(m_runInMessageLoopLock);
					fn = std::move(m_qRunInMessageLoop.front());
					m_qRunInMessageLoop.pop();
				}
				fn();
			}
		}
		
		switch (msg) {
			case WM_DESTROY:
				RemoveAllHooks();
				break;
		}

		m_nWndProcDepth += 1;
		auto res = CallWindowProcW(m_originalGameMainWndProc, hwnd, msg, wParam, lParam);
		m_nWndProcDepth -= 1;
		return res;
	}

	void RemoveAllHooks() {
		MH_DisableHook(MH_ALL_HOOKS);

		if (m_originalGameMainWndProc) {
			SetWindowLongPtrW(m_hGameMainWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalGameMainWndProc));
			m_originalGameMainWndProc = nullptr;
		}
	}

	Internals()
		: m_hGameMainWindow(FindGameMainWindow())
		, m_hUnloadEvent(CreateEventW(nullptr, true, false, nullptr)) {

		const auto onFail = [&](std::function<void()> cb) {
			m_initFailureCleanupList.push_back(cb);
		};
		try {
			Misc::FreeGameMutex::FreeGameMutex();

			ConfigRepository::Config();

			Scintilla_RegisterClasses(g_hInstance);
			onFail([]() { Scintilla_ReleaseResources(); });

			if (!m_hGameMainWindow)
				throw std::exception("Game main window not found!");

			MH_Initialize();
			onFail([]() { MH_Uninitialize(); });
			for (const auto& signature : Signatures::AllSignatures()) {
				signature->Setup();
			}

			namespace WinApiFn = ::App::Signatures::Functions::WinApi;

			// Prevents "Limit frame rate when client is inactive" from making log window unresponsive.
			WinApiFn::SleepEx.SetupHook([this](DWORD dwMilliseconds, BOOL bAlertable) -> DWORD {
				if (m_mainThreadId == GetCurrentThreadId() && dwMilliseconds == 50) {
					const auto res = MsgWaitForMultipleObjectsEx(0, nullptr, dwMilliseconds, QS_ALLEVENTS, bAlertable ? MWMO_ALERTABLE : 0);
					if (res == WAIT_TIMEOUT)
						return 0;
					return WAIT_IO_COMPLETION;
				} else
					return WinApiFn::SleepEx.bridge(dwMilliseconds, bAlertable);
				});

			m_originalGameMainWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hGameMainWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(
				static_cast<WNDPROC>([](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return pInstance->pInternals->OverridenWndProc(hwnd, msg, wParam, lParam); })
				)));
		} catch (std::exception&) {
			for (const auto& cb : m_initFailureCleanupList)
				cb();
			throw;
		}
	}

	~Internals() {
		while (!m_qRunInMessageLoop.empty())
			Sleep(1);

		for (const auto& cb : m_initFailureCleanupList)
			cb();
	}

	void Run() {
		auto& config = ConfigRepository::Config();
		std::vector<std::function<void()>> cleanupCallbacks;
		std::vector<Utils::CallOnDestruction> pendingDestructions;
		const auto onCleanup = [&](std::function<void()> cb) {
			cleanupCallbacks.push_back(cb);
		};

		m_socketHook = std::make_unique<Network::SocketHook>(m_hGameMainWindow);

		QueueRunOnMessageLoop([&]() {
			MH_EnableHook(MH_ALL_HOOKS);

			m_trayWindow = std::make_unique<Window::TrayIcon>(m_hGameMainWindow, [this]() {this->Unload(); });
			onCleanup([this]() { m_trayWindow = nullptr; });

			if (config.AlwaysOnTop)
				SetWindowPos(m_hGameMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			else
				SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			pendingDestructions.push_back(config.AlwaysOnTop.OnChangeListener([&](ConfigItemBase&) {
				if (config.AlwaysOnTop)
					SetWindowPos(m_hGameMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				else
					SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				}));
			onCleanup([this]() { SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); });

			if (config.UseHighLatencyMitigation)
				m_animationLockLatencyHandler = std::make_unique<Feature::AnimationLockLatencyHandler>();
			pendingDestructions.push_back(config.UseHighLatencyMitigation.OnChangeListener([&](ConfigItemBase&) {
				if (config.UseHighLatencyMitigation)
					m_animationLockLatencyHandler = std::make_unique<Feature::AnimationLockLatencyHandler>();
				else
					m_animationLockLatencyHandler = nullptr;
				}));
			onCleanup([this]() { m_animationLockLatencyHandler = nullptr; });

			if (config.UseOpcodeFinder)
				m_ipcTypeFinder = std::make_unique<Feature::IpcTypeFinder>();
			pendingDestructions.push_back(config.UseOpcodeFinder.OnChangeListener([&](ConfigItemBase&) {
				if (config.UseOpcodeFinder)
					m_ipcTypeFinder = std::make_unique<Feature::IpcTypeFinder>();
				else
					m_ipcTypeFinder = nullptr;
				}));
			onCleanup([this]() { m_ipcTypeFinder = nullptr; });

			if (config.ShowLoggingWindow)
				m_logWindow = std::make_unique<Window::Log>();
			pendingDestructions.push_back(config.ShowLoggingWindow.OnChangeListener([&](ConfigItemBase&) {
				if (config.ShowLoggingWindow)
					m_logWindow = std::make_unique<Window::Log>();
				else
					m_logWindow = nullptr;
				}));
			onCleanup([this]() { m_logWindow = nullptr; });

		}, true);

		WaitForSingleObject(m_hUnloadEvent, INFINITE);

		QueueRunOnMessageLoop([&]() {
			config.SetQuitting();
			pendingDestructions.clear();
			for (auto i = cleanupCallbacks.crbegin(); i != cleanupCallbacks.crend(); ++i) 
				(*i)();
		}, true);

		m_socketHook = nullptr;
		RemoveAllHooks();
		MH_Uninitialize();

		SendMessage(m_hGameMainWindow, WM_NULL, 0, 0);
	}

	void QueueRunOnMessageLoop(std::function<void()> f, bool wait = false) {
		if (!wait) {
			{
				std::lock_guard<std::mutex> _lock(m_runInMessageLoopLock);
				m_qRunInMessageLoop.push(f);
			}
			PostMessageW(m_hGameMainWindow, WM_NULL, 0, 0);

		} else {
			Utils::Win32Handle<> hEvent(CreateEvent(nullptr, true, false, nullptr));
			const auto fn = [&f, &hEvent]() {
				try {
					f();
				} catch (...) {

				}
				SetEvent(hEvent);
			};
			{
				std::lock_guard<std::mutex> _lock(m_runInMessageLoopLock);
				m_qRunInMessageLoop.push(fn);
			}
			SendMessageW(m_hGameMainWindow, WM_NULL, 0, 0);
			WaitForSingleObject(hEvent, INFINITE);
		}
	}

	void AddHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal = nullptr) {
		int result = MH_OK;
		QueueRunOnMessageLoop([&]() {
			result = MH_CreateHook(pTarget, pDetour, ppOriginal);
			}, true);
		if (result != MH_OK)
			throw std::exception("AddHook failure");
	}

	int Unload() {
		if (!m_bUnloadDisabled) {
			SetEvent(m_hUnloadEvent);
			return 0;
		}

		return -1;
	}
};

App::App::App() : pInternals(std::make_unique<App::Internals>()) {
}

void App::App::Run() {
	pInternals->Run();
}

int App::App::Unload() {
	return pInternals->Unload();
}

void App::App::QueueRunOnMessageLoop(std::function<void()> f, bool wait) {
	pInternals->QueueRunOnMessageLoop(f, wait);
}

void App::App::AddHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal) {
	pInternals->AddHook(pTarget, pDetour, ppOriginal);
}

static bool l_bFreeLibraryAndExitThread = true;

static DWORD WINAPI DllThread(PVOID param1) {
	{
		App::Misc::Logger logger;
		try {
			pInstance = std::make_unique<App::App>();
			logger.Log(u8"XivAlexander initialized.");
			pInstance->Run();
		} catch (const std::exception& e) {
			if (e.what())
				logger.Format(u8"Error: %s", e.what());
		}
		pInstance = nullptr;
	}
	if (l_bFreeLibraryAndExitThread) {
		Sleep(1000);
		FreeLibraryAndExitThread(g_hInstance, 0);
	}
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
	if (pInstance)
		return 0;
	try {
		Utils::Win32Handle<> hThread(CreateThread(nullptr, 0, DllThread, g_hInstance, 0, nullptr));
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(Utils::FormatString("LoadXivAlexander error: %s\n", e.what()).c_str());
		return -1;
	}
}

extern "C" __declspec(dllexport) int __stdcall DisableUnloading(int bDisable) {
	pInstance->pInternals->m_bUnloadDisabled = !!bDisable;
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall SetFreeLibraryAndExitThread(int use) {
	l_bFreeLibraryAndExitThread = !!use;
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall UnloadXivAlexander() {
	if (pInstance)
		pInstance->Unload();
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall ReloadConfiguration() {
	if (pInstance)
		App::ConfigRepository::Config().Reload(true);
	return 0;
}