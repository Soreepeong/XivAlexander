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

constexpr static auto SecondToMicrosecondMultiplier = 1000000;

struct App::XivAlexApp::Implementation_GameWindow final {
	XivAlexApp* const this_;

	std::mutex m_queueMutex;
	std::queue<std::function<void()>> m_queuedFunctions{};

	HWND m_hWnd{};
	DWORD m_mainThreadId{};
	std::shared_ptr<Misc::Hooks::WndProcFunction> m_subclassHook;

	Utils::CallOnDestruction::Multiple m_cleanup;
	
	const Utils::Win32::Event m_stopEvent;
	const Utils::Win32::Thread m_initThread;  // Must be the last member variable

	Implementation_GameWindow(XivAlexApp* this_)
		: this_(this_)
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

		m_mainThreadId = GetWindowThreadProcessId(m_hWnd, nullptr);

		m_subclassHook = std::make_shared<Misc::Hooks::WndProcFunction>("GameMainWindow", m_hWnd);

		m_cleanup += m_subclassHook->SetHook([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			return SubclassProc(hwnd, msg, wParam, lParam);
		});

		auto& config = this->this_->m_config->Runtime;
		if (config.AlwaysOnTop_GameMainWindow)
			SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		else
			SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		m_cleanup += config.AlwaysOnTop_GameMainWindow.OnChangeListener([&](Config::ItemBase&) {
			if (config.AlwaysOnTop_GameMainWindow)
				SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			else
				SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		});
		m_cleanup += [this]() { SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); };

		IPropertyStorePtr store;
		PROPERTYKEY pkey{};
		PROPVARIANT pv{};
		if (SUCCEEDED(PSGetPropertyKeyFromName(L"System.AppUserModel.ID", &pkey))
			&& SUCCEEDED(SHGetPropertyStoreForWindow(m_hWnd, IID_IPropertyStore, reinterpret_cast<void**>(&store)))
			&& SUCCEEDED(InitPropVariantFromString(L"SquareEnix.FFXIV", &pv))) {
			store->SetValue(pkey, pv);
			PropVariantClear(&pv);
		}
	}

	HWND GetHwnd(bool wait = false) const {
		if (wait && m_initThread.Wait(false, {m_stopEvent}) == WAIT_OBJECT_0 + 1)
			return nullptr;
		return m_hWnd;
	}

	DWORD GetThreadId(bool wait = false) const {
		if (wait && m_initThread.Wait(false, { m_stopEvent }) == WAIT_OBJECT_0 + 1)
			return 0;
		return m_mainThreadId;
	}

	bool IsRunningAtGameMainThread() const {
		return GetCurrentThreadId() == m_mainThreadId;
	}

	void RunOnGameLoop(std::function<void()> f) {
		if (this_->m_bInternalUnloadInitiated)
			return f();
		
		m_initThread.Wait();
		const auto hEvent = Utils::Win32::Event::Create();
		{
			std::lock_guard _lock(m_queueMutex);
			m_queuedFunctions.emplace([this, &f, &hEvent]() {
				try {
					try {
						f();
					} catch (const _com_error& e) {
						if (e.Error() != HRESULT_FROM_WIN32(ERROR_CANCELLED))
							throw Utils::Win32::Error(e);
					}
				} catch (const std::exception& e) {
					this_->m_logger->Log(LogCategory::General, this_->m_config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()), LogLevel::Error);
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

	std::unique_ptr<Window::LogWindow> m_logWindow{};
	std::unique_ptr<Window::MainWindow> m_trayWindow{};

	Misc::Hooks::ImportedFunction<void, UINT> ExitProcess{ "kernel32!ExitProcess", "kernel32.dll", "ExitProcess" };
	Misc::Hooks::ImportedFunction<BOOL, LPMSG, HWND, UINT, UINT, UINT> PeekMessageW{ "user32!PeekMessageW", "user32.dll", "PeekMessageW" };
	Misc::Hooks::ImportedFunction<void, DWORD> Sleep{ "kernel32!Sleep", "kernel32.dll", "Sleep" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, BOOL> SleepEx{ "kernel32!SleepEx", "kernel32.dll", "SleepEx" };
	Misc::Hooks::ImportedFunction<DWORD, HANDLE, DWORD> WaitForSingleObject{ "kernel32!WaitForSingleObject", "kernel32.dll", "WaitForSingleObject" };
	Misc::Hooks::ImportedFunction<DWORD, HANDLE, DWORD, BOOL> WaitForSingleObjectEx{ "kernel32!WaitForSingleObjectEx", "kernel32.dll", "WaitForSingleObjectEx" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, const HANDLE*, BOOL, DWORD> WaitForMultipleObjects{ "kernel32!WaitForMultipleObjects", "kernel32.dll", "WaitForMultipleObjects" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, const HANDLE*, BOOL, DWORD, DWORD> MsgWaitForMultipleObjects{ "user32!MsgWaitForMultipleObjects", "user32.dll", "MsgWaitForMultipleObjects" };

	std::deque<int64_t> LastMessagePumpCounterUs;
	std::set<int64_t> MessagePumpGuaranteeCounterUs;
	int64_t LastWaitForSingleObjectUs{};
	Utils::NumericStatisticsTracker MessagePumpIntervalTrackerUs{ 1024, 0 };
	Utils::NumericStatisticsTracker RenderTimeTakenTrackerUs{ 1024, 0 };
	Utils::NumericStatisticsTracker SocketCallDelayTrackerUs{ 1024, 0 };

	void SetupTrayWindow() {
		if (m_trayWindow)
			return;

		m_trayWindow = std::make_unique<Window::MainWindow>(this_, [this]() {
			if (this->this_->m_bInternalUnloadInitiated)
				return;

			if (const auto err = this_->IsUnloadable(); !err.empty()) {
				Dll::MessageBoxF(m_trayWindow->Handle(), MB_ICONERROR, this->this_->m_config->Runtime.FormatStringRes(IDS_ERROR_UNLOAD_XIVALEXANDER, err));
				return;
			}

			this->this_->m_bInternalUnloadInitiated = true;
			void(Utils::Win32::Thread(L"XivAlexander::App::XivAlexApp::Implementation::SetupTrayWindow::XivAlexUnloader", []() {
				XivAlexDll::DisableAllApps(nullptr);
			}, Utils::Win32::LoadedModule::LoadMore(Dll::Module())));
		});
	}

	Implementation(XivAlexApp* this_)
		: this_(this_) {
	}

	void Cleanup() {
		m_cleanup.Clear();
		if (const auto hwnd = this_->m_pGameWindow->GetHwnd(false)) {
			// Make sure our window procedure hook isn't in progress
			SendMessageW(hwnd, WM_NULL, 0, 0);
		}
	}

	~Implementation() {
		Cleanup();
	}

	bool ShouldSkipSleep(DWORD dwMilliseconds) const {
		static uint16_t s_counter = 0;
		if (dwMilliseconds > 1)
			return false;
		if (!this_->m_config->Runtime.UseMoreCpuTime)
			return false;
		if (GetForegroundWindow() != this_->m_pGameWindow->GetHwnd(false))
			return false;
		if (!++s_counter)
			SwitchToThread();
		return true;
	}

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
			const auto quickTerminate = this_->m_config->Runtime.TerminateOnExitProcess.Value();

			this->this_->m_bInternalUnloadInitiated = true;

			if (this->m_trayWindow)
				SendMessageW(this->m_trayWindow->Handle(), WM_CLOSE, exitCode, quickTerminate ? 2 : 1);
			WaitForSingleObject(this->this_->m_hCustomMessageLoop, INFINITE);

			if (quickTerminate)
				TerminateProcess(GetCurrentProcess(), exitCode);

			XivAlexDll::DisableAllApps(nullptr);

			// hook is released, and "this" should be invalid at this point.
			::ExitProcess(exitCode);
		});

		m_cleanup += PeekMessageW.SetHook([this, lastRemoveMsg = UINT{}](LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) mutable->BOOL {
			if (GetCurrentThreadId() == this_->m_pGameWindow->GetThreadId(false)) {
				if (lastRemoveMsg == wRemoveMsg) {
					auto nowUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier);
					int64_t waitUntilCounterUs = 0;

					if (LastWaitForSingleObjectUs && !LastMessagePumpCounterUs.empty()) {
						RenderTimeTakenTrackerUs.AddValue(LastWaitForSingleObjectUs - LastMessagePumpCounterUs.back());
						LastWaitForSingleObjectUs = 0;
					}

					// Ignore failed guarantees
					while (!MessagePumpGuaranteeCounterUs.empty() && *MessagePumpGuaranteeCounterUs.begin() <= nowUs)
						MessagePumpGuaranteeCounterUs.erase(MessagePumpGuaranteeCounterUs.begin());

					if (!MessagePumpGuaranteeCounterUs.empty() && *MessagePumpGuaranteeCounterUs.begin() - nowUs <= MessagePumpIntervalTrackerUs.Latest()) {
						waitUntilCounterUs = *MessagePumpGuaranteeCounterUs.begin();
						MessagePumpGuaranteeCounterUs.erase(MessagePumpGuaranteeCounterUs.begin());
					}

					auto recordPumpInterval = false;
					if (const auto intervalUs = this_->m_config->Runtime.GameLoopInputAcceptIntervalUs; intervalUs && waitUntilCounterUs == 0) {
						recordPumpInterval = true;
						waitUntilCounterUs = (1 + nowUs / intervalUs) * intervalUs;
					}

					if (const auto socketSelectUs = m_socketHook ? m_socketHook->GetLastSocketSelectCounterUs() : 0)
						SocketCallDelayTrackerUs.AddValue(socketSelectUs - LastMessagePumpCounterUs.back());

					if (waitUntilCounterUs > 0 && !LastMessagePumpCounterUs.empty() && this_->m_config->Runtime.SynchronizeProcessing) {
						const auto useMoreCpuTime = this_->m_config->Runtime.UseMoreCpuTime.Value();
						while (waitUntilCounterUs > (nowUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier))) {
							if (useMoreCpuTime)
								void(0);
							else
								::Sleep(0);
						}
						LastMessagePumpCounterUs.push_back(nowUs);
					} else {
						LastMessagePumpCounterUs.push_back(nowUs);
						recordPumpInterval = true;
					}
					if (recordPumpInterval && LastMessagePumpCounterUs.size() >= 2)
						MessagePumpIntervalTrackerUs.AddValue(LastMessagePumpCounterUs[LastMessagePumpCounterUs.size() - 1] - LastMessagePumpCounterUs[LastMessagePumpCounterUs.size() - 2]);
					if (LastMessagePumpCounterUs.size() > 2 && LastMessagePumpCounterUs.back() - LastMessagePumpCounterUs.front() >= SecondToMicrosecondMultiplier)
						LastMessagePumpCounterUs.pop_front();

				} else
					lastRemoveMsg = wRemoveMsg;
			}
			return PeekMessageW.bridge(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
			});

		m_cleanup += Sleep.SetHook([&](DWORD dwMilliseconds) {
			if (!ShouldSkipSleep(dwMilliseconds))
				Sleep.bridge(dwMilliseconds);
			});

		m_cleanup += SleepEx.SetHook([&](DWORD dwMilliseconds, BOOL bAlertable) -> DWORD {
			// Note: if the user have conditional frame rate limit, then SleepEx(50) will be called.
			if (bAlertable || !ShouldSkipSleep(dwMilliseconds))
				return SleepEx.bridge(dwMilliseconds, bAlertable);

			return 0;
			});

		m_cleanup += WaitForSingleObject.SetHook([&](HANDLE hHandle, DWORD dwMilliseconds) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			else if (dwMilliseconds == INFINITE && this_->m_pGameWindow->IsRunningAtGameMainThread())
				LastWaitForSingleObjectUs = Utils::GetHighPerformanceCounter(1000000);
			return WaitForSingleObject.bridge(hHandle, dwMilliseconds);
			});

		m_cleanup += WaitForSingleObjectEx.SetHook([&](HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			return WaitForSingleObjectEx.bridge(hHandle, dwMilliseconds, bAlertable);
			});

		m_cleanup += WaitForMultipleObjects.SetHook([&](DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			return WaitForMultipleObjects.bridge(nCount, lpHandles, bWaitAll, dwMilliseconds);
			});

		m_cleanup += MsgWaitForMultipleObjects.SetHook([&](DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			return MsgWaitForMultipleObjects.bridge(nCount, lpHandles, bWaitAll, dwMilliseconds, dwWakeMask);
			});
	}
};

App::XivAlexApp::Implementation_GameWindow::~Implementation_GameWindow() {
	m_stopEvent.Set();
	m_initThread.Wait();
	m_cleanup.Clear();
}

Utils::ListenerManager<App::XivAlexApp, void, App::XivAlexApp&> App::XivAlexApp::OnAppCreated;

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

	if (!m_bInternalUnloadInitiated) {
		// Being destructed from DllMain(Process detach).
		// Should have been cleaned up first, but couldn't get a chance,
		// so the only thing possible is to force quit, or an error message will appear.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	m_pImpl->Cleanup();
}

void App::XivAlexApp::CustomMessageLoopBody() {
	const auto activationContextCleanup = Dll::ActivationContext().With();

	m_pImpl->Load();

	m_logger->Log(LogCategory::General, m_config->Runtime.GetLangId(), IDS_LOG_XIVALEXANDER_INITIALIZED);

	try {
		Misc::FreeGameMutex::FreeGameMutex();
	} catch (const std::exception& e) {
		m_logger->Format<LogLevel::Warning>(LogCategory::General, m_config->Runtime.GetLangId(), IDS_ERROR_FREEGAMEMUTEX, e.what());
	}

	m_loadCompleteEvent.Set();
	OnAppCreated(*this);

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

_Maybenull_ HWND App::XivAlexApp::GetGameWindowHandle(bool wait) const {
	return m_pGameWindow->GetHwnd(wait);
}

DWORD App::XivAlexApp::GetGameWindowThreadId(bool wait) const {
	return m_pGameWindow->GetThreadId(wait);
}

void App::XivAlexApp::RunOnGameLoop(std::function<void()> f) {
	m_pGameWindow->RunOnGameLoop(std::move(f));
}

std::string App::XivAlexApp::IsUnloadable() const {
	if (const auto pszDisabledReason = Dll::GetUnloadDisabledReason())
		return pszDisabledReason;

	if (Dll::IsLoadedAsDependency())
		return Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_NOUNLOADREASON_DEPENDENCY));

	if (m_pImpl == nullptr || m_pGameWindow == nullptr)
		return "";

	if (Feature::GameResourceOverrider::Enabled())
		return Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_NOUNLOADREASON_MODACTIVE));

	if (m_pImpl->m_socketHook && !m_pImpl->m_socketHook->IsUnloadable())
		return Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_NOUNLOADREASON_SOCKET));

	if (m_pGameWindow->m_subclassHook && !m_pGameWindow->m_subclassHook->IsDisableable())
		return Utils::ToUtf8(m_config->Runtime.GetStringRes(IDS_NOUNLOADREASON_WNDPROC));

	return "";
}

App::Network::SocketHook* App::XivAlexApp::GetSocketHook() {
	return m_pImpl->m_socketHook.get();
}

const Utils::NumericStatisticsTracker& App::XivAlexApp::GetMessagePumpIntervalTrackerUs() const {
	return m_pImpl->MessagePumpIntervalTrackerUs;
}

const Utils::NumericStatisticsTracker& App::XivAlexApp::GetRenderTimeTakenTrackerUs() const {
	return m_pImpl->RenderTimeTakenTrackerUs;
}

const Utils::NumericStatisticsTracker& App::XivAlexApp::GetSocketCallDelayTrackerUs() const {
	return m_pImpl->SocketCallDelayTrackerUs;
}

void App::XivAlexApp::GuaranteePumpBeginCounter(int64_t nextInUs) {
	if (nextInUs > 0)
		m_pImpl->MessagePumpGuaranteeCounterUs.insert(Utils::GetHighPerformanceCounter(1000000) + nextInUs);
}

static std::unique_ptr<App::XivAlexApp> s_xivAlexApp;

App::XivAlexApp* App::XivAlexApp::GetCurrentApp() {
	return s_xivAlexApp.get();
}

size_t __stdcall XivAlexDll::EnableXivAlexander(size_t bEnable) {
	if (Dll::IsLoadedAsDependency())
		return -1;

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
		if (bEnable)
			Dll::MessageBoxF(nullptr, MB_ICONERROR | MB_OK,
				FindStringResourceEx(Dll::Module(), IDS_ERROR_LOAD) + 1,
				e.what());
		return -1;
	}
}

size_t __stdcall XivAlexDll::ReloadConfiguration(void* lpReserved) {
	const auto config = App::Config::Acquire();
	config->Runtime.Reload({}, true);
	config->Game.Reload({}, true);
	return 0;
}
