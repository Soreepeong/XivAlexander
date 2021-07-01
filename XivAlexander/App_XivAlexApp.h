#pragma once

#include <memory>

#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"

namespace App {
	namespace Misc {
		class DebuggerDetectionDisabler;
	}
	
	class XivAlexApp {
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;
		const std::shared_ptr<Misc::Logger> m_logger;
		
		class Implementation;
		friend class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

		bool m_bInterrnalUnloadInitiated = false;
		std::thread m_customMessageLoop;

	public:
		XivAlexApp();
		~XivAlexApp();

		[[nodiscard]] HWND GetGameWindowHandle() const;

		void RunOnGameLoop(std::function<void()> f);
		[[nodiscard]] bool IsUnloadable() const;
		
		[[nodiscard]] Network::SocketHook* GetSocketHook();

		void CheckUpdates(bool silent = true);
	};
}