#include "pch.h"
#include "App_XivAlexApp.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_ConfigRepository.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Feature_EffectApplicationDelayLogger.h"
#include "App_Feature_GameResourceOverrider.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_FreeGameMutex.h"
#include "App_Misc_Hooks.h"
#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"
#include "App_Window_LogWindow.h"
#include "App_Window_MainWindow.h"
#include "DllMain.h"
#include "resource.h"

struct App::XivAlexApp::Implementation_GameWindow final {
	XivAlexApp* const this_;

	std::mutex m_queueMutex;
	std::queue<std::function<void()>> m_queuedFunctions{};

	HWND m_hWnd;
	std::shared_ptr<Misc::Hooks::WndProcFunction> m_subclassHook;

	Utils::CallOnDestruction::Multiple m_cleanup;

	const Utils::Win32::Event m_windowFoundEvent;
	const Utils::Win32::Event m_stopEvent;
	const Utils::Win32::Thread m_initThread;  // Must be the last member variable

	Implementation_GameWindow(XivAlexApp* this_)
		: this_(this_)
		, m_hWnd(nullptr)
		, m_windowFoundEvent(Utils::Win32::Event::Create())
		, m_stopEvent(Utils::Win32::Event::Create())
		, m_initThread(Utils::Win32::Thread(L"XivAlexApp::Implementation_GameWindow::Initializer", [this]() { InitializeThreadBody(); })) {
	}

	~Implementation_GameWindow();

	void InitializeThreadBody() {
		do {
			if (m_stopEvent.Wait(100) == WAIT_OBJECT_0)
				return;
			m_hWnd = Dll::FindGameMainWindow(false);
		} while (!m_hWnd);
		m_windowFoundEvent.Set();

		m_subclassHook = std::make_shared<Misc::Hooks::WndProcFunction>("GameMainWindow", m_hWnd);

		m_cleanup += m_subclassHook->SetHook([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			return SubclassProc(hwnd, msg, wParam, lParam);
		});

		auto& config = this->this_->m_config->Runtime;
		if (config.AlwaysOnTop)
			SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		else
			SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		m_cleanup += config.AlwaysOnTop.OnChangeListener([&](Config::ItemBase&) {
			if (config.AlwaysOnTop)
				SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			else
				SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		});
		m_cleanup += [this]() { SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); };
	}

	HWND GetHwnd() const {
		if (m_windowFoundEvent.Wait(false, { m_stopEvent }) == WAIT_OBJECT_0 + 1)
			return nullptr;
		return m_hWnd;
	}

	void RunOnGameLoop(std::function<void()> f) {
		if (this_->m_bInterrnalUnloadInitiated)
			return f();

		if (!m_hWnd && !Dll::FindGameMainWindow(false))
			return f();

		m_initThread.Wait();
		const auto hEvent = Utils::Win32::Event::Create();
		{
			std::lock_guard _lock(m_queueMutex);
			m_queuedFunctions.emplace([this, &f, &hEvent]() {
				try {
					f();
				} catch (const std::exception& e) {
					this_->m_logger->Log(LogCategory::General, this_->m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()), LogLevel::Error);
				} catch (const _com_error& e) {
					this_->m_logger->Log(LogCategory::General, this_->m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, static_cast<const wchar_t*>(e.Description())), LogLevel::Error);
				} catch (...) {
					this_->m_logger->Log(LogCategory::General, this_->m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, L"?"), LogLevel::Error);
				}
				hEvent.Set();
			});
		}

		SendMessageW(m_hWnd, WM_NULL, 0, 0);
		hEvent.Wait();
	}

	LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		while (!m_queuedFunctions.empty()) {
			std::function<void()> fn;
			{
				std::lock_guard _lock(m_queueMutex);
				fn = std::move(m_queuedFunctions.front());
				m_queuedFunctions.pop();
			}
			fn();
		}

		return m_subclassHook->bridge(hwnd, msg, wParam, lParam);
	}
};

struct App::XivAlexApp::Implementation final {
	XivAlexApp* const this_;

	Utils::CallOnDestruction::Multiple m_cleanup;

	std::unique_ptr<Network::SocketHook> m_socketHook{};
	std::unique_ptr<Feature::AnimationLockLatencyHandler> m_animationLockLatencyHandler{};
	std::unique_ptr<Feature::IpcTypeFinder> m_ipcTypeFinder{};
	std::unique_ptr<Feature::AllIpcMessageLogger> m_allIpcMessageLogger{};
	std::unique_ptr<Feature::EffectApplicationDelayLogger> m_effectApplicationDelayLogger{};
	std::unique_ptr<Feature::GameResourceOverrider> m_gameResourceOverrider{};

	std::unique_ptr<Window::LogWindow> m_logWindow{};
	std::unique_ptr<Window::MainWindow> m_trayWindow{};

	Misc::Hooks::ImportedFunction<void, UINT> ExitProcess{ "ExitProcess", "kernel32.dll", "ExitProcess" };

	void SetupTrayWindow() {
		if (m_trayWindow)
			return;

		m_trayWindow = std::make_unique<Window::MainWindow>(this_, [this]() {
			if (this->this_->m_bInterrnalUnloadInitiated)
				return;

			if (const auto err = this_->IsUnloadable(); !err.empty()) {
				Utils::Win32::MessageBoxF(m_trayWindow->Handle(), MB_ICONERROR, this->this_->m_config->Runtime.GetStringRes(IDS_APP_NAME),
					this->this_->m_config->Runtime.FormatStringRes(IDS_ERROR_UNLOAD_XIVALEXANDER, err));
				return;
			}

			this->this_->m_bInterrnalUnloadInitiated = true;
			void(Utils::Win32::Thread(L"XivAlexander::App::XivAlexApp::Implementation::SetupTrayWindow::XivAlexUnloader", []() {
				XivAlexDll::DisableAllApps(nullptr);
			}, Utils::Win32::LoadedModule::LoadMore(Dll::Module())));
		});
	}

	Implementation(XivAlexApp* this_)
		: this_(this_) {
	}

	~Implementation();

	void Load() {
		Scintilla_RegisterClasses(Dll::Module());
		m_cleanup += []() { Scintilla_ReleaseResources(); };

		m_socketHook = std::make_unique<Network::SocketHook>(this->this_);
		m_cleanup += [this]() {
			m_socketHook = nullptr;
		};

		SetupTrayWindow();
		m_cleanup += [this]() { m_trayWindow = nullptr; };

		auto& config = this_->m_config->Runtime;

		if (config.UseHighLatencyMitigation)
			m_animationLockLatencyHandler = std::make_unique<Feature::AnimationLockLatencyHandler>(m_socketHook.get());
		m_cleanup += config.UseHighLatencyMitigation.OnChangeListener([&](Config::ItemBase&) {
			if (config.UseHighLatencyMitigation)
				m_animationLockLatencyHandler = std::make_unique<Feature::AnimationLockLatencyHandler>(m_socketHook.get());
			else
				m_animationLockLatencyHandler = nullptr;
		});
		m_cleanup += [this]() { m_animationLockLatencyHandler = nullptr; };

		if (config.UseOpcodeFinder)
			m_ipcTypeFinder = std::make_unique<Feature::IpcTypeFinder>(m_socketHook.get());
		m_cleanup += config.UseOpcodeFinder.OnChangeListener([&](Config::ItemBase&) {
			if (config.UseOpcodeFinder)
				m_ipcTypeFinder = std::make_unique<Feature::IpcTypeFinder>(m_socketHook.get());
			else
				m_ipcTypeFinder = nullptr;
		});
		m_cleanup += [this]() { m_ipcTypeFinder = nullptr; };

		if (config.UseAllIpcMessageLogger)
			m_allIpcMessageLogger = std::make_unique<Feature::AllIpcMessageLogger>(m_socketHook.get());
		m_cleanup += config.UseAllIpcMessageLogger.OnChangeListener([&](Config::ItemBase&) {
			if (config.UseAllIpcMessageLogger)
				m_allIpcMessageLogger = std::make_unique<Feature::AllIpcMessageLogger>(m_socketHook.get());
			else
				m_allIpcMessageLogger = nullptr;
		});
		m_cleanup += [this]() { m_allIpcMessageLogger = nullptr; };

		if (config.UseEffectApplicationDelayLogger)
			m_effectApplicationDelayLogger = std::make_unique<Feature::EffectApplicationDelayLogger>(m_socketHook.get());
		m_cleanup += config.UseEffectApplicationDelayLogger.OnChangeListener([&](Config::ItemBase&) {
			if (config.UseEffectApplicationDelayLogger)
				m_effectApplicationDelayLogger = std::make_unique<Feature::EffectApplicationDelayLogger>(m_socketHook.get());
			else
				m_effectApplicationDelayLogger = nullptr;
		});
		m_cleanup += [this]() { m_effectApplicationDelayLogger = nullptr; };

		m_gameResourceOverrider = std::make_unique<Feature::GameResourceOverrider>();
		m_cleanup += [this]() { m_gameResourceOverrider = nullptr; };

		if (config.ShowLoggingWindow)
			m_logWindow = std::make_unique<Window::LogWindow>();
		m_cleanup += config.ShowLoggingWindow.OnChangeListener([&](Config::ItemBase&) {
			if (config.ShowLoggingWindow)
				m_logWindow = std::make_unique<Window::LogWindow>();
			else
				m_logWindow = nullptr;
		});
		m_cleanup += [this]() { m_logWindow = nullptr; };

		m_cleanup += ExitProcess.SetHook([this](UINT exitCode) {
			this->this_->m_bInterrnalUnloadInitiated = true;

			if (this->m_trayWindow)
				SendMessageW(this->m_trayWindow->Handle(), WM_CLOSE, 0, 1);
			WaitForSingleObject(this->this_->m_hCustomMessageLoop, INFINITE);

			XivAlexDll::DisableAllApps(nullptr);

			// hook is released, and "this" should be invalid at this point.
			::ExitProcess(exitCode);
		});
	}
};

App::XivAlexApp::Implementation_GameWindow::~Implementation_GameWindow() {
	m_stopEvent.Set();
	m_initThread.Wait();
	m_cleanup.Clear();
}

App::XivAlexApp::Implementation::~Implementation() {
	m_cleanup.Clear();
}

App::XivAlexApp::XivAlexApp()
	: m_module(Utils::Win32::LoadedModule::LoadMore(Dll::Module()))
	, m_detectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
	, m_logger(Misc::Logger::Acquire())
	, m_config(Config::Acquire())
	, m_pImpl(std::make_unique<Implementation>(this))
	, m_pGameWindow(std::make_unique<Implementation_GameWindow>(this))
	, m_loadCompleteEvent(Utils::Win32::Event::Create())
	, m_hCustomMessageLoop(L"XivAlexander::App::XivAlexApp::CustomMessageLoopBody", [this]() { CustomMessageLoopBody(); }) {
	m_loadCompleteEvent.Wait();
}

App::XivAlexApp::~XivAlexApp() {
	m_loadCompleteEvent.Set();

	if (!IsUnloadable().empty()) {
		// Unloading despite IsUnloadable being set.
		// Either process is terminating to begin with, or something went wrong,
		// so terminating self is the right choice.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	if (m_pImpl->m_trayWindow)
		SendMessageW(m_pImpl->m_trayWindow->Handle(), WM_CLOSE, 0, 1);

	m_hCustomMessageLoop.Wait();

	if (!m_bInterrnalUnloadInitiated) {
		// Being destructed from DllMain(Process detach).
		// Should have been cleaned up first, but couldn't get a chance,
		// so the only thing possible is to force quit, or an error message will appear.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	m_pImpl->m_cleanup.Clear();
}

void App::XivAlexApp::CustomMessageLoopBody() {
	const auto activationContextCleanup = Dll::ActivationContext().With();

	m_pImpl->Load();

	m_logger->Log(LogCategory::General, m_config->Runtime.GetLangId(), IDS_LOG_XIVALEXANDER_INITIALIZED);

	try {
		Misc::FreeGameMutex::FreeGameMutex();
	} catch (std::exception& e) {
		m_logger->Format<LogLevel::Warning>(LogCategory::General, m_config->Runtime.GetLangId(), IDS_ERROR_FREEGAMEMUTEX, e.what());
	}

	m_loadCompleteEvent.Set();

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		auto processed = false;

		for (const auto pWindow : Window::BaseWindow::All()) {
			if (!pWindow || pWindow->IsDestroyed())
				continue;

			const auto hWnd = pWindow->Handle();

			if (const auto hAccel = pWindow->GetThreadAcceleratorTable()) {
				if (TranslateAcceleratorW(hWnd, hAccel, &msg)) {
					processed = true;
					break;
				}
			}

			if (hWnd != msg.hwnd && !IsChild(hWnd, msg.hwnd))
				continue;

			if (const auto hAccel = pWindow->GetWindowAcceleratorTable()) {
				if (TranslateAcceleratorW(hWnd, hAccel, &msg)) {
					processed = true;
					break;
				}
			}

			if (pWindow->IsDialogLike() && ((processed = IsDialogMessageW(msg.hwnd, &msg))))
				break;
		}

		if (!processed) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
}

HWND App::XivAlexApp::GetGameWindowHandle() const {
	return m_pGameWindow->GetHwnd();
}

void App::XivAlexApp::RunOnGameLoop(std::function<void()> f) {
	m_pGameWindow->RunOnGameLoop(std::move(f));
}

std::string App::XivAlexApp::IsUnloadable() const {
	if (const auto pszDisabledReason = Dll::GetUnloadDisabledReason())
		return pszDisabledReason;

	if (Dll::IsLoadedAsDependency())
		return "Loaded as dependency";  // TODO: create string resource

	if (m_pImpl == nullptr || m_pGameWindow == nullptr)
		return "";

	if (!m_pImpl->m_gameResourceOverrider->CanUnload())
		return "Cannot unload when overlaying game resources";  // TODO: create string resource

	if (m_pImpl->m_socketHook && !m_pImpl->m_socketHook->IsUnloadable())
		return Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_UNLOAD_SOCKET));

	if (m_pGameWindow->m_subclassHook && !m_pGameWindow->m_subclassHook->IsDisableable())
		return Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_ERROR_UNLOAD_WNDPROC));

	return "";
}

App::Network::SocketHook* App::XivAlexApp::GetSocketHook() {
	return m_pImpl->m_socketHook.get();
}

static std::unique_ptr<App::XivAlexApp> s_xivAlexApp;

size_t __stdcall XivAlexDll::EnableXivAlexander(size_t bEnable) {
	if (!!bEnable == !!s_xivAlexApp)
		return 0;
	try {
		if (s_xivAlexApp && !bEnable) {
			if (const auto reason = s_xivAlexApp->IsUnloadable(); !reason.empty()) {
				Utils::Win32::DebugPrint(L"Cannot unload: {}", reason);
				return -2;
			}
		}
		s_xivAlexApp = bEnable ? std::make_unique<App::XivAlexApp>() : nullptr;
		return 0;
	} catch (const std::exception& e) {
		Utils::Win32::DebugPrint(L"LoadXivAlexander error: {}\n", e.what());
		return -1;
	}
}

size_t __stdcall XivAlexDll::ReloadConfiguration(void* lpReserved) {
	if (s_xivAlexApp) {
		const auto config = App::Config::Acquire();
		config->Runtime.Reload(true);
		config->Game.Reload(true);
	}
	return 0;
}
