#pragma once

namespace App::Misc {
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

		[[nodiscard]] bool IsDebuggerActuallyPresent() const;

		void BreakIfDebugged() const {
			if (IsDebuggerActuallyPresent()) {
				__try {
					__debugbreak();
				} __except (EXCEPTION_CONTINUE_EXECUTION) {
					// do nothing
				}
			}
		}

		virtual ~DebuggerDetectionDisabler();
	};
}
