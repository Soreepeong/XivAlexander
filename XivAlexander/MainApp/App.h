#pragma once

#include <memory>
#include <xivres/util.listener_manager.h>
#include "Utils/NumericStatisticsTracker.h"
#include "Utils/Win32.h"
#include "Utils/Win32/Handle.h"
#include "Utils/Win32/LoadedModule.h"

namespace XivAlexander::Apps::MainApp::Features {
	class NetworkTimingHandler;
	class MainThreadTimingHandler;
	class SocketHook;
	class PatchCode;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class ResourceOverrider;
}

namespace XivAlexander::Misc {
	class DebuggerDetectionDisabler;
	class Logger;
	class OpcodeGuesser;
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
		[[nodiscard]] bool IsGameWindowFocused() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] std::string IsUnloadable() const;

		[[nodiscard]] Features::SocketHook& GetSocketHook();
		[[nodiscard]] Features::Modding::ResourceOverrider& GetResourceOverrider();
		[[nodiscard]] std::optional<Features::NetworkTimingHandler>& GetNetworkTimingHandler();
		[[nodiscard]] std::optional<Features::MainThreadTimingHandler>& GetMainThreadTimingHelper();

		static xivres::util::listener_manager<App, void, App&> OnAppCreated;
	};
}
