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
		const Utils::Win32::Closeable::LoadedModule m_module;
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::shared_ptr<Config> m_config;
		
		class Implementation;
		friend class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		bool m_bInterrnalUnloadInitiated = false;
		bool m_bMainWindowDestroyed = false;
		Utils::Win32::Closeable::Handle m_hCustomMessageLoop;

	public:
		XivAlexApp();
		~XivAlexApp();

	private:
		DWORD CustomMessageLoopBody();

	public:
		[[nodiscard]] HWND GetGameWindowHandle() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] std::string IsUnloadable() const;
		
		[[nodiscard]] Network::SocketHook* GetSocketHook();

		void CheckUpdates(bool silent = true);
	};
}
