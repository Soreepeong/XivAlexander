#pragma once

#include <memory>
#include <XivAlexanderCommon/Utils/Win32/LoadedModule.h>

#include "Misc/DebuggerDetectionDisabler.h"

namespace XivAlexander::EntryPoint {
	class EntryPointApp {
		const Utils::Win32::LoadedModule m_module;
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;

		struct Implementation;
		friend struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		EntryPointApp();
		~EntryPointApp();

		void SetFlags(size_t flags);
	};
}
