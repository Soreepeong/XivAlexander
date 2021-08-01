#pragma once
namespace App::Misc {
	class DebuggerDetectionDisabler {
		class Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	protected:
		class DebuggerDetectionDisablerCreator;
		friend class DebuggerDetectionDisablerCreator;
		DebuggerDetectionDisabler();
		static std::weak_ptr<DebuggerDetectionDisabler> s_instance;
	
	public:
		static std::shared_ptr<DebuggerDetectionDisabler> Acquire();

		[[nodiscard]]
		bool IsDebuggerActuallyPresent() const;
		void BreakIfDebugged() const;
		
		virtual ~DebuggerDetectionDisabler();
	};
}
