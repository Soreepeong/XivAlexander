#pragma once

namespace XivAlexander::Misc {
	class DebuggerDetectionDisabler {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	protected:
		class DebuggerDetectionDisablerCreator;
		friend class DebuggerDetectionDisablerCreator;
		DebuggerDetectionDisabler();
		static std::weak_ptr<DebuggerDetectionDisabler> s_instance;

	public:
		static std::shared_ptr<DebuggerDetectionDisabler> Acquire();

		virtual ~DebuggerDetectionDisabler();
	};
}
