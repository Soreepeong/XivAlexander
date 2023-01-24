#pragma once

#include <memory>
#include <XivAlexanderCommon/Utils/ListenerManager.h>
#include <XivAlexanderCommon/Utils/NumericStatisticsTracker.h>
#include <XivAlexanderCommon/Utils/Win32.h>
#include <XivAlexanderCommon/Utils/Win32/Handle.h>
#include <XivAlexanderCommon/Utils/Win32/LoadedModule.h>

namespace XivAlexander::Apps::MainApp::Internal {
	class GameResourceOverrider;
	class NetworkTimingHandler;
	class MainThreadTimingHandler;
	class SocketHook;
	class PatchCode;
}

namespace XivAlexander::Misc {
	class DebuggerDetectionDisabler;
	class Logger;
}

namespace XivAlexander::Apps::MainApp {
	class App {
		struct Implementation;
		friend struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		struct Implementation_GameWindow;
		friend struct Implementation_GameWindow;
		std::unique_ptr<Implementation_GameWindow> m_pGameWindow;

		bool m_bInternalUnloadInitiated = false;

		const Utils::Win32::Event m_loadCompleteEvent;

		// needs to be last, as "this" needs to be done initializing
		const Utils::Win32::Thread m_myLoop;

	public:
		App();
		~App();

	private:
		void CustomMessageLoopBody();

	public:
		[[nodiscard]] HWND GetGameWindowHandle(bool wait = false) const;
		[[nodiscard]] DWORD GetGameWindowThreadId(bool wait = false) const;
		[[nodiscard]] bool IsRunningOnGameMainThread() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] std::string IsUnloadable() const;

		[[nodiscard]] Internal::SocketHook& GetSocketHook();
		[[nodiscard]] Internal::GameResourceOverrider& GetGameResourceOverrider();
		[[nodiscard]] std::optional<Internal::NetworkTimingHandler>& GetNetworkTimingHandler();
		[[nodiscard]] std::optional<Internal::MainThreadTimingHandler>& GetMainThreadTimingHelper();

		static Utils::ListenerManager<App, void, App&> OnAppCreated;
	};
}
