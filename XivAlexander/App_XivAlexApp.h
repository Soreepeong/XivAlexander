#pragma once

#include <memory>
#include <XivAlexanderCommon/Utils_Win32.h>
#include <XivAlexanderCommon/Utils_Win32_Handle.h>
#include <XivAlexanderCommon/Utils_Win32_LoadedModule.h>

namespace App {
	namespace Network {
		class SocketHook;
	}

	namespace Misc {
		class DebuggerDetectionDisabler;
		class Logger;
	}

	class Config;

	class XivAlexApp {
		const Utils::Win32::LoadedModule m_module;
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::shared_ptr<Config> m_config;

		struct Implementation;
		friend struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		struct Implementation_GameWindow;
		friend struct Implementation_GameWindow;
		std::unique_ptr<Implementation_GameWindow> m_pGameWindow;

		bool m_bInterrnalUnloadInitiated = false;

		const Utils::Win32::Event m_loadCompleteEvent;

		// needs to be last, as "this" needs to be done initializing
		Utils::Win32::Thread m_hCustomMessageLoop;

	public:
		XivAlexApp();
		~XivAlexApp();

	private:
		void CustomMessageLoopBody();

	public:
		[[nodiscard]] _Maybenull_ HWND GetGameWindowHandle() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] std::string IsUnloadable() const;

		[[nodiscard]] Network::SocketHook* GetSocketHook();
	};
}
