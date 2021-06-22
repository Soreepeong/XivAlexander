#include "pch.h"
#include "App_App.h"
#include "App_Network_SocketHook.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Feature_EffectApplicationDelayLogger.h"
#include "App_Window_LogWindow.h"
#include "App_Window_MainWindow.h"

App::App* App::App::s_pInstance = nullptr;
bool App::App::s_bUnloadDisabled = false;

static HWND FindGameMainWindow() {
	HWND hwnd = nullptr;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid == GetCurrentProcessId())
			break;
	}
	return hwnd;
}

class App::App::Implementation {
	App* const this_;

public:
	const HWND m_hGameMainWindow;
	std::vector<Utils::CallOnDestruction> m_cleanupPendingDestructions;

	std::mutex m_runInMessageLoopLock;
	std::queue<std::function<void()>> m_qRunInMessageLoop;

	std::unique_ptr<Network::SocketHook> m_socketHook;
	std::unique_ptr<Feature::AnimationLockLatencyHandler> m_animationLockLatencyHandler;
	std::unique_ptr<Feature::IpcTypeFinder> m_ipcTypeFinder;
	std::unique_ptr<Feature::AllIpcMessageLogger> m_allIpcMessageLogger;
	std::unique_ptr<Feature::EffectApplicationDelayLogger> m_effectApplicationDelayLogger;

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
		const auto res = CallWindowProcW(m_originalGameMainWndProc, hwnd, msg, wParam, lParam);
		m_nWndProcDepth -= 1;
		return res;
	}

	const LONG_PTR m_overridenGameMainWndProc = reinterpret_cast<LONG_PTR>(
		static_cast<WNDPROC>([](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return s_pInstance->m_pImpl->OverridenWndProc(hwnd, msg, wParam, lParam); })
		);

	void OnCleanup(std::function<void()> cb) {
		m_cleanupPendingDestructions.emplace_back(std::move(cb));
	}

	void OnCleanup(Utils::CallOnDestruction cb) {
		m_cleanupPendingDestructions.push_back(std::move(cb));
	}

	void SetupTrayWindow() {
		if (m_trayWindow)
			return;

		m_trayWindow = std::make_unique<Window::Main>(m_hGameMainWindow, [this]() {
			try {
				this_->Unload();
			} catch (std::exception& e) {
				SetupTrayWindow();
				MessageBoxW(m_trayWindow->GetHandle(), Utils::FromUtf8(Utils::FormatString("Unable to unload XivAlexander: %s", e.what())).c_str(), L"XivAlexander", MB_ICONERROR);
			}
			});
	}

	Implementation(App* this_)
		: this_(this_)
		, m_hGameMainWindow(FindGameMainWindow()) {
	}

	~Implementation() {
		Config::Instance().SetQuitting();
		while (!m_cleanupPendingDestructions.empty())
			m_cleanupPendingDestructions.pop_back();

		s_pInstance = nullptr;
	}

	void Load() {
		try {
			s_pInstance = this_;
			OnCleanup([]() { s_pInstance = nullptr; });

			Config::Instance();
			OnCleanup([]() { Config::DestroyInstance(); });

			Scintilla_RegisterClasses(g_hInstance);
			OnCleanup([]() { Scintilla_ReleaseResources(); });

			if (!m_hGameMainWindow)
				throw std::runtime_error("Game main window not found!");

			MH_Initialize();
			OnCleanup([this]() { MH_Uninitialize(); });
			for (const auto& signature : Signatures::AllSignatures())
				signature->Setup();

			m_originalGameMainWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hGameMainWindow, GWLP_WNDPROC, m_overridenGameMainWndProc));
			OnCleanup([this]() {
				this->this_->QueueRunOnMessageLoop([]() {
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

			s_pInstance = nullptr;
			throw;
		}
	}
};

App::App::App()
	: m_pImpl(std::make_unique<Implementation>(this)) {
	if (s_pInstance)
		throw std::runtime_error("App already initialized");

	m_pImpl->Load();
}

App::App::~App() = default;

void App::App::QueueRunOnMessageLoop(std::function<void()> f, bool wait) {
	if (!wait) {
		{
			std::lock_guard _lock(m_pImpl->m_runInMessageLoopLock);
			m_pImpl->m_qRunInMessageLoop.push(f);
		}
		PostMessageW(m_pImpl->m_hGameMainWindow, WM_NULL, 0, 0);

	} else {
		Utils::Win32::Closeable::Handle hEvent(CreateEventW(nullptr, true, false, nullptr),
			INVALID_HANDLE_VALUE,
			"App::QueueRunOnMessageLoop/CreateEventW");
		const auto fn = [&f, &hEvent]() {
			try {
				f();
			} catch (const std::exception& e) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::General, "Unexpected error occurred: %s", e.what());
			} catch (const _com_error& e) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::General, "Unexpected error occurred: %s",
					Utils::ToUtf8(static_cast<const wchar_t*>(e.Description())));
			} catch (...) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::General, "Unexpected error occurred");
			}
			SetEvent(hEvent);
		};
		{
			std::lock_guard _lock(m_pImpl->m_runInMessageLoopLock);
			m_pImpl->m_qRunInMessageLoop.push(fn);
		}
		SendMessageW(m_pImpl->m_hGameMainWindow, WM_NULL, 0, 0);
		WaitForSingleObject(hEvent, INFINITE);
	}
}

void App::App::Run() {
	QueueRunOnMessageLoop([&]() {
		for (const auto& signature : Signatures::AllSignatures())
			signature->Startup();
		}, true);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		bool dispatchMessage = true;
		for (const auto pWindow : Window::BaseWindow::GetAllOpenWindows()) {
			const auto hWnd = pWindow->GetHandle();
			const auto hAccel = pWindow->GetAcceleratorTable();
			if (hAccel && (hWnd == msg.hwnd || IsChild(hWnd, msg.hwnd))) {
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

int App::App::Unload() {
	if (s_bUnloadDisabled)
		throw std::runtime_error("Unloading is currently disabled.");

	if (GetWindowLongPtrW(m_pImpl->m_hGameMainWindow, GWLP_WNDPROC) != m_pImpl->m_overridenGameMainWndProc)
		throw std::runtime_error("Something has hooked the game process after XivAlexander, so you cannot unload XivAlexander until that other thing has been unloaded.");

	if (m_pImpl->m_trayWindow)
		SendMessage(m_pImpl->m_trayWindow->GetHandle(), WM_CLOSE, 0, 1);
	return 0;
}

void App::App::SetDisableUnloading(bool v) {
	s_bUnloadDisabled = v;
}

App::App* App::App::Instance() {
	return s_pInstance;
}
