#pragma once

#include <memory>

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
		
		class Implementation;
		friend class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		bool m_bInterrnalUnloadInitiated = false;
		
		// needs to be last, as "this" needs to be done initializing
		Utils::Win32::Thread m_hCustomMessageLoop;

	public:
		XivAlexApp();
		~XivAlexApp();

	private:
		void CustomMessageLoopBody();

	public:
		[[nodiscard]] HWND GetGameWindowHandle() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] std::string IsUnloadable() const;
		
		[[nodiscard]] Network::SocketHook* GetSocketHook();
	};
}
