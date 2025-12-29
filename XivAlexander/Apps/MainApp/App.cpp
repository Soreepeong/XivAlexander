#include "pch.h"
#include "Apps/MainApp/App.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Utils/Win32/Resource.h>

#include "Config.h"
#include "Apps/MainApp/Internal/AllIpcMessageLogger.h"
#include "Apps/MainApp/Internal/MainThreadTimingHandler.h"
#include "Apps/MainApp/Internal/NetworkTimingHandler.h"
#include "Apps/MainApp/Internal/GameResourceOverrider.h"
#include "Apps/MainApp/Internal/IpcTypeFinder.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Apps/MainApp/Internal/PatchCode.h"
#include "Misc/DebuggerDetectionDisabler.h"
#include "Misc/FreeGameMutex.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"
#include "Apps/MainApp/Window/LogWindow.h"
#include "Apps/MainApp/Window/MainWindow.h"
#include "resource.h"
#include "XivAlexander.h"

struct XivAlexander::Apps::MainApp::App::Implementation_GameWindow final {
	MainApp::App& App;
	const std::shared_ptr<Config> Config;

	std::mutex RunOnGameLoopMtx;
	std::queue<std::function<void()>> RunOnGameLoopQueue{};

	HWND Handle{};
	DWORD ThreadId{};
	std::shared_ptr<Misc::Hooks::WndProcFunction> SubclassHook;

	Utils::CallOnDestruction::Multiple Cleanup;

	const Utils::Win32::Event ReadyEvent = Utils::Win32::Event::Create();
	const Utils::Win32::Event StopEvent = Utils::Win32::Event::Create();
	const Utils::Win32::Thread InitThread;  // Must be the last member variable

	Implementation_GameWindow(MainApp::App& app);
	~Implementation_GameWindow();

	void InitializeThreadBody();

	HWND GetHwnd(bool wait = false) const;
	DWORD GetThreadId(bool wait = false) const;

	void RunOnGameLoop(std::function<void()> f);

	LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

struct XivAlexander::Apps::MainApp::App::Implementation {
	MainApp::App& App;
	const Utils::Win32::LoadedModule MyModule;
	const std::shared_ptr<Misc::DebuggerDetectionDisabler> DebuggerDetectionDisabler;
	const std::shared_ptr<Misc::Logger> Logger;
	const std::shared_ptr<Config> Config;

	Utils::CallOnDestruction::Multiple Cleanup;

	std::optional<Internal::PatchCode> PatchCode;
	
	// Mandatory, but initialize late
	std::optional<Internal::SocketHook> SocketHook;
	std::optional<Internal::GameResourceOverrider> GameResourceOverrider;

	// Optional
	std::optional<Internal::NetworkTimingHandler> NetworkTimingHandler;
	std::optional<Internal::MainThreadTimingHandler> MainThreadTimingHandler;
	std::optional<Internal::IpcTypeFinder> IpcTypeFinder;
	std::optional<Internal::AllIpcMessageLogger> AllIpcMessageLogger;

	std::optional<Window::LogWindow> LogWindow;
	std::optional<Window::MainWindow> MainWindow;

	Misc::Hooks::ImportedFunction<void, UINT> ExitProcess{ "kernel32!ExitProcess", "kernel32.dll", "ExitProcess" };

	void SetupMainWindow() {
		if (MainWindow)
			return;

		MainWindow.emplace(App, [this]() {
			if (this->App.m_bInternalUnloadInitiated)
				return;

			if (const auto err = App.IsUnloadable(); !err.empty()) {
				Dll::MessageBoxF(MainWindow->Handle(), MB_ICONERROR, Config->Runtime.FormatStringRes(IDS_ERROR_UNLOAD_XIVALEXANDER, err));
				return;
			}

			this->App.m_bInternalUnloadInitiated = true;
			void(Utils::Win32::Thread(L"XivAlexander::App::XivAlexApp::Implementation::SetupTrayWindow::XivAlexUnloader", []() {
				Dll::DisableAllApps(nullptr);
				}, Utils::Win32::LoadedModule::LoadMore(Dll::Module())));
			});
	}

	Implementation(MainApp::App& app)
		: App(app)
		, MyModule(Utils::Win32::LoadedModule::LoadMore(Dll::Module()))
		, DebuggerDetectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
		, Logger(Misc::Logger::Acquire())
		, Config(Config::Acquire()) {

		Cleanup += [&app]() {
			if (const auto hwnd = app.m_pGameWindow->GetHwnd(false)) {
				// Make sure our window procedure hook isn't in progress
				SendMessageW(hwnd, WM_NULL, 0, 0);
			}
		};
	}

	~Implementation() {
		Cleanup.Clear();
	}

	void LoadAfterThisConstruct() {
		PatchCode.emplace(App);
		
		SocketHook.emplace(App);
		Cleanup += [this]() { SocketHook.reset(); };

		GameResourceOverrider.emplace(App);
		Cleanup += [this]() { GameResourceOverrider.reset(); };

		Scintilla_RegisterClasses(Dll::Module());
		Cleanup += []() { Scintilla_ReleaseResources(); };

		SetupMainWindow();
		Cleanup += [this]() { MainWindow.reset(); };

		Cleanup += ExitProcess.SetHook([this](UINT exitCode) {
			if (this->MainWindow)
				SendMessageW(this->MainWindow->Handle(), WM_CLOSE, exitCode, 2);
			TerminateProcess(GetCurrentProcess(), exitCode);
			});

		Cleanup += Config->Runtime.UseNetworkTimingHandler.AddAndCallOnBoolChange(
			[this]() { NetworkTimingHandler.emplace(App); },
			[this]() { NetworkTimingHandler.reset(); });

		Cleanup += Config->Runtime.UseMainThreadTimingHandler.AddAndCallOnBoolChange(
			[this]() { MainThreadTimingHandler.emplace(App); },
			[this]() { MainThreadTimingHandler.reset(); });

		Cleanup += Config->Runtime.UseOpcodeFinder.AddAndCallOnBoolChange(
			[this]() { IpcTypeFinder.emplace(App); },
			[this]() { IpcTypeFinder.reset(); });

		Cleanup += Config->Runtime.UseAllIpcMessageLogger.AddAndCallOnBoolChange(
			[this]() { AllIpcMessageLogger.emplace(App); },
			[this]() { AllIpcMessageLogger.reset(); });

		Cleanup += Config->Runtime.ShowLoggingWindow.AddAndCallOnBoolChange(
			[this]() { LogWindow.emplace(); },
			[this]() { LogWindow.reset(); });
	}
};

XivAlexander::Apps::MainApp::App::Implementation_GameWindow::Implementation_GameWindow(MainApp::App& app)
	: App(app)
	, Config(Config::Acquire())
	, InitThread(Utils::Win32::Thread(L"XivAlexApp::Implementation_GameWindow::Initializer", [this]() { InitializeThreadBody(); })) {
}

XivAlexander::Apps::MainApp::App::Implementation_GameWindow::~Implementation_GameWindow() {
	StopEvent.Set();
	ReadyEvent.Set();
	InitThread.Wait();
	Cleanup.Clear();
}

void XivAlexander::Apps::MainApp::App::Implementation_GameWindow::InitializeThreadBody() {
	do {
		if (StopEvent.Wait(100) == WAIT_OBJECT_0)
			return;
		Handle = Dll::FindGameMainWindow(false);
	} while (!Handle);

	ThreadId = GetWindowThreadProcessId(Handle, nullptr);

	SubclassHook = std::make_shared<Misc::Hooks::WndProcFunction>("GameMainWindow", Handle);

	Cleanup += SubclassHook->SetHook([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		return SubclassProc(hwnd, msg, wParam, lParam);
		});

	auto& config = App.m_pImpl->Config->Runtime;
	if (config.AlwaysOnTop_GameMainWindow)
		SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	else
		SetWindowPos(Handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	Cleanup += config.AlwaysOnTop_GameMainWindow.OnChange([&]() {
		if (config.AlwaysOnTop_GameMainWindow)
			SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		else
			SetWindowPos(Handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		});
	Cleanup += [this]() { SetWindowPos(Handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); };

	Cleanup += Config->Runtime.AddProcessIDToGameWindowTitle.AddAndCallOnBoolChange(
		[this]() {
			std::wstring buf(GetWindowTextLengthW(Handle) + 1, L'\0');
			buf.resize(GetWindowTextW(Handle, buf.data(), static_cast<int>(buf.size())));

			const auto suffix = std::format(L" ({})", GetCurrentProcessId());
			if (buf.ends_with(suffix))
				return;

			buf += suffix;
			SetWindowTextW(Handle, buf.c_str());
		},
		[this]() {
			std::wstring buf(GetWindowTextLengthW(Handle) + 1, L'\0');
			buf.resize(GetWindowTextW(Handle, buf.data(), static_cast<int>(buf.size())));

			const auto suffix = std::format(L" ({})", GetCurrentProcessId());
			if (!buf.ends_with(suffix))
				return;

			buf.resize(buf.size() - suffix.size());
			SetWindowTextW(Handle, buf.c_str());
		});

	ReadyEvent.Set();
	RunOnGameLoop([&]() {
		const char* lastStep = "";
		try {
			try {
				IPropertyStorePtr store;
				PROPVARIANT pv{};

				lastStep = "SHGetPropertyStoreForWindow";
				if (const auto r = SHGetPropertyStoreForWindow(Handle, IID_IPropertyStore, reinterpret_cast<void**>(&store)); FAILED(r))
					throw _com_error(r);

				lastStep = "InitPropVariantFromString";
				if (const auto r = InitPropVariantFromString(L"SquareEnix.FFXIV", &pv); FAILED(r))
					throw _com_error(r);

				lastStep = "store->SetValue";
				if (const auto r = store->SetValue(PKEY_AppUserModel_ID, pv); FAILED(r))
					throw _com_error(r);

				PropVariantClear(&pv);
			} catch (const _com_error& e) {
				if (e.Error() != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
					throw Utils::Win32::Error(e);
				}
			}
		} catch (const Utils::Win32::Error& e) {
			App.m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::General, "Failed to set System.AppUserModel.ID for the game window at step {}: {}", lastStep, e.what());
		}
		});
}

void XivAlexander::Apps::MainApp::App::Implementation_GameWindow::RunOnGameLoop(std::function<void()> f) {
	if (App.m_bInternalUnloadInitiated)
		return f();

	ReadyEvent.Wait();
	const auto hEvent = Utils::Win32::Event::Create();
	{
		std::lock_guard _lock(RunOnGameLoopMtx);
		RunOnGameLoopQueue.emplace([this, &f, &hEvent]() {
			try {
				try {
					f();
				} catch (const _com_error& e) {
					if (e.Error() != HRESULT_FROM_WIN32(ERROR_CANCELLED))
						throw Utils::Win32::Error(e);
				}
			} catch (const std::exception& e) {
				App.m_pImpl->Logger->Log(LogCategory::General, App.m_pImpl->Config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, e.what()), LogLevel::Error);
			} catch (...) {
				App.m_pImpl->Logger->Log(LogCategory::General, App.m_pImpl->Config->Runtime.FormatStringRes(IDS_ERROR_UNEXPECTED, L"?"), LogLevel::Error);
			}
			hEvent.Set();
			});
	}

	SendMessageW(Handle, WM_NULL, 0, 0);
	hEvent.Wait();
}

HWND XivAlexander::Apps::MainApp::App::Implementation_GameWindow::GetHwnd(bool wait /*= false*/) const {
	if (wait && ReadyEvent.Wait(false, { StopEvent }) == WAIT_OBJECT_0 + 1)
		return nullptr;
	return Handle;
}

DWORD XivAlexander::Apps::MainApp::App::Implementation_GameWindow::GetThreadId(bool wait /*= false*/) const {
	if (wait && ReadyEvent.Wait(false, { StopEvent }) == WAIT_OBJECT_0 + 1)
		return 0;
	return ThreadId;
}

LRESULT CALLBACK XivAlexander::Apps::MainApp::App::Implementation_GameWindow::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	while (!RunOnGameLoopQueue.empty()) {
		std::function<void()> fn;
		{
			std::lock_guard _lock(RunOnGameLoopMtx);
			fn = std::move(RunOnGameLoopQueue.front());
			RunOnGameLoopQueue.pop();
		}
		fn();
	}

	return SubclassHook->bridge(hwnd, msg, wParam, lParam);
}

Utils::ListenerManager<XivAlexander::Apps::MainApp::App, void, XivAlexander::Apps::MainApp::App&> XivAlexander::Apps::MainApp::App::OnAppCreated;

XivAlexander::Apps::MainApp::App::App()
	: m_pImpl(std::make_unique<Implementation>(*this))
	, m_pGameWindow(std::make_unique<Implementation_GameWindow>(*this))
	, m_loadCompleteEvent(Utils::Win32::Event::Create())
	, m_myLoop(L"XivAlexander::App::XivAlexApp::CustomMessageLoopBody", [this]() { CustomMessageLoopBody(); }) {
	m_loadCompleteEvent.Wait();
}

XivAlexander::Apps::MainApp::App::~App() {
	m_loadCompleteEvent.Set();

	if (!IsUnloadable().empty()) {
		// Unloading despite IsUnloadable being set.
		// Either process is terminating to begin with, or something went wrong,
		// so terminating self is the right choice.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	if (m_pImpl->MainWindow)
		SendMessageW(m_pImpl->MainWindow->Handle(), WM_CLOSE, 0, 1);

	m_myLoop.Wait();

	if (!m_bInternalUnloadInitiated) {
		// Being destructed from DllMain(Process detach).
		// Should have been cleaned up first, but couldn't get a chance,
		// so the only thing possible is to force quit, or an error message will appear.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	m_pImpl.reset();
}

void XivAlexander::Apps::MainApp::App::CustomMessageLoopBody() {
	const auto activationContextCleanup = Dll::ActivationContext().With();

	m_pImpl->LoadAfterThisConstruct();

	m_pImpl->Logger->Log(LogCategory::General, m_pImpl->Config->Runtime.GetLangId(), IDS_LOG_XIVALEXANDER_INITIALIZED);

	try {
		Misc::FreeGameMutex::FreeGameMutex();
	} catch (const std::exception& e) {
		m_pImpl->Logger->Format<LogLevel::Warning>(LogCategory::General, m_pImpl->Config->Runtime.GetLangId(), IDS_ERROR_FREEGAMEMUTEX, e.what());
	}

	m_loadCompleteEvent.Set();
	OnAppCreated(*this);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		auto processed = false;

		for (const auto pWindow : Apps::MainApp::Window::BaseWindow::All()) {
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

HWND XivAlexander::Apps::MainApp::App::GetGameWindowHandle(bool wait) const {
	return m_pGameWindow->GetHwnd(wait);
}

DWORD XivAlexander::Apps::MainApp::App::GetGameWindowThreadId(bool wait) const {
	return m_pGameWindow->GetThreadId(wait);
}

bool XivAlexander::Apps::MainApp::App::IsRunningOnGameMainThread() const {
	return GetCurrentThreadId() == m_pGameWindow->ThreadId;
}

void XivAlexander::Apps::MainApp::App::RunOnGameLoop(std::function<void()> f) {
	m_pGameWindow->RunOnGameLoop(std::move(f));
}

std::string XivAlexander::Apps::MainApp::App::IsUnloadable() const {
	if (const auto pszDisabledReason = Dll::GetUnloadDisabledReason())
		return pszDisabledReason;

	if (Dll::IsLoadedAsDependency())
		return Utils::ToUtf8(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_DEPENDENCY));

	if (m_pImpl == nullptr || m_pGameWindow == nullptr)
		return "";

	if (m_pImpl->GameResourceOverrider->GetVirtualSqPacks())
		return Utils::ToUtf8(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_MODACTIVE));

	if (m_pImpl->SocketHook && !m_pImpl->SocketHook->IsUnloadable())
		return Utils::ToUtf8(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_SOCKET));

	if (m_pGameWindow->SubclassHook && !m_pGameWindow->SubclassHook->IsDisableable())
		return Utils::ToUtf8(m_pImpl->Config->Runtime.GetStringRes(IDS_NOUNLOADREASON_WNDPROC));

	return "";
}

XivAlexander::Apps::MainApp::Internal::SocketHook& XivAlexander::Apps::MainApp::App::GetSocketHook() {
	return *m_pImpl->SocketHook;
}

XivAlexander::Apps::MainApp::Internal::GameResourceOverrider& XivAlexander::Apps::MainApp::App::GetGameResourceOverrider() {
	return *m_pImpl->GameResourceOverrider;
}

std::optional<XivAlexander::Apps::MainApp::Internal::NetworkTimingHandler>& XivAlexander::Apps::MainApp::App::GetNetworkTimingHandler() {
	return m_pImpl->NetworkTimingHandler;
}

std::optional<XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler>& XivAlexander::Apps::MainApp::App::GetMainThreadTimingHelper() {
	return m_pImpl->MainThreadTimingHandler;
}

size_t __stdcall Dll::EnableXivAlexander(size_t bEnable) {
	static std::unique_ptr<XivAlexander::Apps::MainApp::App> s_app;

	if (Dll::IsLoadedAsDependency())
		return -1;

	if (!!bEnable == !!s_app)
		return 0;
	try {
		if (s_app && !bEnable) {
			if (const auto reason = s_app->IsUnloadable(); !reason.empty()) {
				Utils::Win32::DebugPrint(L"Cannot unload: {}", reason);
				return -2;
			}
		}
		s_app = bEnable ? std::make_unique<XivAlexander::Apps::MainApp::App>() : nullptr;
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

size_t __stdcall Dll::ReloadConfiguration(void* lpReserved) {
	const auto config = XivAlexander::Config::Acquire();
	config->Runtime.Reload();
	config->Game.Reload();
	return 0;
}
