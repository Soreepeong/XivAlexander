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
	
	class XivAlexApp {
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;
		const std::shared_ptr<Misc::Logger> m_logger;
		
		class Implementation;
		friend class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		bool m_bInterrnalUnloadInitiated = false;
		bool m_bMainWindowDestroyed = false;
		std::thread m_customMessageLoop;

	public:
		XivAlexApp();
		~XivAlexApp();

		[[nodiscard]] HWND GetGameWindowHandle() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] std::string IsUnloadable() const;
		
		[[nodiscard]] Network::SocketHook* GetSocketHook();

		void CheckUpdates(bool silent = true);
	};
}
