#pragma once

#include <memory>

namespace App {
	namespace Misc {
		class DebuggerDetectionDisabler;
	}

	class InjectOnCreateProcessApp {
		const Utils::Win32::LoadedModule m_module;
		const std::shared_ptr<Misc::DebuggerDetectionDisabler> m_detectionDisabler;

		class Implementation;
		friend class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		InjectOnCreateProcessApp();
		~InjectOnCreateProcessApp();
		
		void SetFlags(size_t flags);
	};
}
