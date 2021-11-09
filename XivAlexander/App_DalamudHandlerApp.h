#pragma once

#include <memory>
#include <XivAlexanderCommon/Utils_Win32_LoadedModule.h>

#include "App_Misc_DebuggerDetectionDisabler.h"

namespace App {
	class DalamudHandlerApp {
		const Utils::Win32::LoadedModule m_module;
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;

		struct Implementation;
		friend struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		DalamudHandlerApp();
		~DalamudHandlerApp();

		static void LoadDalamudHandler();
	};
}
