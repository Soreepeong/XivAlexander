#include "pch.h"
#include "XivAlexander/XivAlexander.h"
#include "App_XivAlexApp.h"
#include "App_Misc_Hooks.h"
#include "App_Network_SocketHook.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Feature_EffectApplicationDelayLogger.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_FreeGameMutex.h"
#include "App_Window_LogWindow.h"
#include "App_Window_MainWindow.h"

static HWND FindGameMainWindow() {
	HWND hwnd = nullptr;
	while ((hwnd = FindWindowExW(nullptr, hwnd, L"FFXIVGAME", nullptr))) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);

		if (pid == GetCurrentProcessId())
			break;
	}
	if (hwnd == nullptr)
		throw std::runtime_error("Game window not found");
	return hwnd;
}

class App::XivAlexApp::Implementation {
	XivAlexApp* const this_;

public:
	const HWND m_hGameMainWindow;
	Utils::CallOnDestruction::Multiple m_cleanup;

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

	std::shared_ptr<Misc::Hooks::WndProcFunction> m_gameWindowSubclass;

	Misc::Hooks::ImportedFunction<void, UINT> ExitProcess{"ExitProcess", "kernel32.dll", "ExitProcess"};

	LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
			{
				const auto bridger = std::move(m_gameWindowSubclass);
				this->this_->m_bInterrnalUnloadInitiated = true;
				this->this_->m_bMainWindowDestroyed = true;

				XivAlexDll::DisableAllApps(nullptr);

				return bridger->bridge(hwnd, msg, wParam, lParam);
			}
		}

		m_nWndProcDepth += 1;
		const auto res = m_gameWindowSubclass->bridge(hwnd, msg, wParam, lParam);
		m_nWndProcDepth -= 1;
		
		return res;
	}

	void SetupTrayWindow() {
		if (m_trayWindow)
			return;

		m_trayWindow = std::make_unique<Window::Main>(this_, [this]() {
			if (this->this_->m_bInterrnalUnloadInitiated)
				return;
			
			if (const auto err = this_->IsUnloadable(); !err.empty()) {
				Utils::Win32::MessageBoxF(m_trayWindow->GetHandle(), MB_ICONERROR, L"XivAlexander", L"Unable to unload XivAlexander: {}", err);
				return;
			}

			this->this_->m_bInterrnalUnloadInitiated = true;
			Utils::Win32::Closeable::Thread(L"XivAlexUnloader", [](){
				XivAlexDll::DisableAllApps(nullptr);
			}, g_hInstance);
		});
	}

	Implementation(XivAlexApp* this_)
		: this_(this_)
		, m_hGameMainWindow(FindGameMainWindow())
		, m_gameWindowSubclass(std::make_shared<Misc::Hooks::WndProcFunction>("GameMainWindow", m_hGameMainWindow)) {
	}

	~Implementation() {
		m_cleanup.Clear();
	}

	void Load() {
		Scintilla_RegisterClasses(g_hInstance);
		m_cleanup += []() { Scintilla_ReleaseResources(); };

		if (!m_hGameMainWindow)
			throw std::runtime_error("Game main window not found!");

		m_cleanup += m_gameWindowSubclass->SetHook([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
			return SubclassProc(hwnd, msg, wParam, lParam);
			});

		m_socketHook = std::make_unique<Network::SocketHook>(this->this_);
		m_cleanup += [this]() {
			m_socketHook = nullptr;
		};

		SetupTrayWindow();
		m_cleanup += [this]() { m_trayWindow = nullptr; };

		auto& config = this_->m_config->Runtime;

		if (config.AlwaysOnTop)
			SetWindowPos(m_hGameMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		else
			SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		m_cleanup += config.AlwaysOnTop.OnChangeListener([&](Config::ItemBase&) {
			if (config.AlwaysOnTop)
				SetWindowPos(m_hGameMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			else
				SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			});
		m_cleanup += [this]() { SetWindowPos(m_hGameMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); };

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
			m_logWindow = std::make_unique<Window::Log>();
		m_cleanup += config.ShowLoggingWindow.OnChangeListener([&](Config::ItemBase&) {
			if (config.ShowLoggingWindow)
				m_logWindow = std::make_unique<Window::Log>();
			else
				m_logWindow = nullptr;
			});
		m_cleanup += [this]() { m_logWindow = nullptr; };

		m_cleanup += ExitProcess.SetHook([this](UINT exitCode) {
			this->this_->m_bInterrnalUnloadInitiated = true;
			this->this_->m_bMainWindowDestroyed = true;
					
			if (this->m_trayWindow)
				SendMessage(this->m_trayWindow->GetHandle(), WM_CLOSE, 0, 1);
			WaitForSingleObject(this->this_->m_hCustomMessageLoop, INFINITE);
			
			XivAlexDll::DisableAllApps(nullptr);

			// hook is released, and "this" should be invalid at this point.
			::ExitProcess(exitCode);
		});
	}
};

App::XivAlexApp::XivAlexApp()
	: m_module(Utils::Win32::Closeable::LoadedModule::From(g_hInstance))
	, m_detectionDisabler(Misc::DebuggerDetectionDisabler::Acquire())
	, m_logger(Misc::Logger::Acquire())
	, m_config(Config::Acquire())
	, m_pImpl(std::make_unique<Implementation>(this)) {

	try {
		m_hCustomMessageLoop = Utils::Win32::Closeable::Handle(
			CreateThread(nullptr, 0, [](void* pThis) {
				return static_cast<XivAlexApp*>(pThis)->CustomMessageLoopBody();
			}, this, 0, nullptr),
			Utils::Win32::Closeable::Handle::Null, "XivAlexApp::CreateThread(CustomMessageLoop)"
		);
	} catch (...) {
		throw;
	}
}

App::XivAlexApp::~XivAlexApp() {
	if (!IsUnloadable().empty())
		std::abort();

	if (m_pImpl->m_trayWindow)
		SendMessage(m_pImpl->m_trayWindow->GetHandle(), WM_CLOSE, 0, 1);

	WaitForSingleObject(m_hCustomMessageLoop, INFINITE);

	if (!m_bInterrnalUnloadInitiated) {
		// Being destructed from DllMain(Process detach).
		// Should have been cleaned up first, but couldn't get a chance,
		// so the only thing possible is to force quit, or an error message will appear.
		TerminateProcess(GetCurrentProcess(), 0);
	}

	m_pImpl->m_cleanup.Clear();
}

DWORD App::XivAlexApp::CustomMessageLoopBody() {
	m_pImpl->Load();
	
	m_logger->Log(LogCategory::General, u8"XivAlexander initialized.");

	try {
		Misc::FreeGameMutex::FreeGameMutex();
	} catch (std::exception& e) {
		m_logger->Format<LogLevel::Warning>(LogCategory::General, "Failed to free game mutex: {}", e.what());
	}

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)) {
		bool dispatchMessage = true;
		
		for (const auto pWindow : {
			static_cast<Window::BaseWindow*>(m_pImpl->m_logWindow.get()),
			static_cast<Window::BaseWindow*>(m_pImpl->m_trayWindow.get()),
		}) {
			if (!pWindow)
				continue;
			const auto hWnd = pWindow->GetHandle();
			const auto hAccel = pWindow->GetAcceleratorTable();
			if (hAccel && (hWnd == msg.hwnd || IsChild(hWnd, msg.hwnd))) {
				if (TranslateAcceleratorW(hWnd, hAccel, &msg)) {
					dispatchMessage = false;
					break;
				}
			}
		}
		if (IsDialogMessageW(msg.hwnd, &msg))
			dispatchMessage = false;
		
		if (dispatchMessage) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	return 0;
}

HWND App::XivAlexApp::GetGameWindowHandle() const {
	return m_pImpl->m_hGameMainWindow;
}

void App::XivAlexApp::RunOnGameLoop(std::function<void()> f) {
	if (m_bMainWindowDestroyed) {
		f();
		return;
	}
	
	const Utils::Win32::Closeable::Handle hEvent(CreateEventW(nullptr, true, false, nullptr),
		INVALID_HANDLE_VALUE,
		"XivAlexApp::RunOnGameLoop/CreateEventW");
	{
		std::lock_guard _lock(m_pImpl->m_runInMessageLoopLock);
		m_pImpl->m_qRunInMessageLoop.emplace([this, &f, &hEvent]() {
			try {
				f();
			} catch (const std::exception& e) {
				m_logger->Format<LogLevel::Error>(LogCategory::General, "Unexpected error occurred: {}", e.what());
			} catch (const _com_error& e) {
				m_logger->Format<LogLevel::Error>(LogCategory::General, "Unexpected error occurred: {}",
					Utils::ToUtf8(static_cast<const wchar_t*>(e.Description())));
			} catch (...) {
				m_logger->Format<LogLevel::Error>(LogCategory::General, "Unexpected error occurred");
			}
			SetEvent(hEvent);
			});
	}
	SendMessageW(m_pImpl->m_hGameMainWindow, WM_NULL, 0, 0);
	WaitForSingleObject(hEvent, INFINITE);
}

std::string App::XivAlexApp::IsUnloadable() const {
	if (m_pImpl == nullptr)
		return "";

	if (m_pImpl->m_socketHook && !m_pImpl->m_socketHook->IsUnloadable())
		return "Another module has hooked socket functions over XivAlexander. Try unloading that other module first.";
	
	if (!m_pImpl->m_gameWindowSubclass->IsDisableable())
		return "Another module has hooked window procedure over XivAlexander. Try unloading that other module first.";

	return "";
}

App::Network::SocketHook* App::XivAlexApp::GetSocketHook() {
	return m_pImpl->m_socketHook.get();
}

void App::XivAlexApp::CheckUpdates(bool silent) {
	try {
		const auto [selfFileVersion, selfProductVersion] = Utils::Win32::FormatModuleVersionString(g_hInstance);
		const auto up = XivAlex::CheckUpdates();
		const auto remoteS = Utils::StringSplit(up.Name.substr(1), ".");
		const auto localS = Utils::StringSplit(selfProductVersion, ".");
		std::vector<int> remote, local;
		for (const auto& s : remoteS)
			remote.emplace_back(std::stoi(s));
		for (const auto& s : localS)
			local.emplace_back(std::stoi(s));
		if (local.size() != 4 || remote.size() != 4)
			throw std::runtime_error("Invalid format specification");
		if (local > remote) {
			const auto s = std::format("No updates available; you have the most recent version {}.{}.{}.{}; server version is {}.{}.{}.{} released at {:%Ec}",
				local[0], local[1], local[2], local[3], remote[0], remote[1], remote[2], remote[3], up.PublishDate);
			m_logger->Log(LogCategory::General, s);
			if (!silent)
				MessageBoxW(nullptr, Utils::FromUtf8(s).c_str(), L"XivAlexander", MB_OK);
		} else if (local == remote) {
			const auto s = std::format("No updates available; you have the most recent version {}.{}.{}.{}, released at {:%Ec}", local[0], local[1], local[2], local[3], up.PublishDate);
			m_logger->Log(LogCategory::General, s);
			if (!silent)
				MessageBoxW(nullptr, Utils::FromUtf8(s).c_str(), L"XivAlexander", MB_OK);
		} else {
			const auto s = std::format("New version {}.{}.{}.{}, released at {:%Ec}, is available. Local version is {}.{}.{}.{}", remote[0], remote[1], remote[2], remote[3], up.PublishDate, local[0], local[1], local[2], local[3]);
			m_logger->Log(LogCategory::General, s);
			if (!silent) {
				switch (Utils::Win32::MessageBoxF(nullptr, MB_YESNOCANCEL, L"XivAlexander", std::format(
					L"{}\n\n"
					L"Press Yes to check out the changelog,\n"
					L"Press No to download the file right now, or\n"
					L"Press Cancel to do nothing.",
					s
				).c_str())) {
					case IDYES:
						ShellExecuteW(nullptr, L"open", L"https://github.com/Soreepeong/XivAlexander/releases", nullptr, nullptr, SW_SHOW);
						break;
					case IDNO:
						ShellExecuteW(nullptr, L"open", Utils::FromUtf8(up.DownloadLink).c_str(), nullptr, nullptr, SW_SHOW);
						break;
				}
			}
		}
	} catch (const std::exception& e) {
		m_logger->Format<LogLevel::Error>(LogCategory::General, "Failed to check for updates: {}", e.what());
	}
}

static std::unique_ptr<App::XivAlexApp> s_xivAlexApp;

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::EnableXivAlexander(size_t bEnable) {
	if (!!bEnable == !!s_xivAlexApp)
		return 0;
	try {
		s_xivAlexApp = bEnable ? std::make_unique<App::XivAlexApp>() : nullptr;
		return 0;
	} catch (const std::exception& e) {
		OutputDebugStringA(std::format("LoadXivAlexander error: {}\n", e.what()).c_str());
		return -1;
	}
}

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::ReloadConfiguration(void* lpReserved) {
	if (s_xivAlexApp) {
		const auto config = App::Config::Acquire();
		config->Runtime.Reload(true);
		config->Game.Reload(true);
	}
	return 0;
}
