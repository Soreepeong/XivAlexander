#include "pch.h"
#include "App_Misc_DebuggerDetectionDisabler.h"
#include "App_Misc_Hooks.h"

struct App::Misc::DebuggerDetectionDisabler::Implementation {
	Hooks::PointerFunction<BOOL> IsDebuggerPresent{ "DebuggerDetectionDisabler::IsDebuggerPresent", ::IsDebuggerPresent };
	Utils::CallOnDestruction::Multiple m_cleanup;

	Implementation() {
		Utils::Win32::DebugPrint(L"DebuggerDetectionDisabler: New");
		m_cleanup += IsDebuggerPresent.SetHook([]() {return FALSE; });
	}

	~Implementation() {
		Utils::Win32::DebugPrint(L"DebuggerDetectionDisabler: Destroy");
	}
};

class App::Misc::DebuggerDetectionDisabler::DebuggerDetectionDisablerCreator : public DebuggerDetectionDisabler {
public:
	DebuggerDetectionDisablerCreator() = default;
	~DebuggerDetectionDisablerCreator() override = default;
};

std::shared_ptr<App::Misc::DebuggerDetectionDisabler> App::Misc::DebuggerDetectionDisabler::Acquire() {
	auto r = s_instance.lock();
	if (!r) {
		static std::mutex mtx;
		std::lock_guard lock(mtx);

		r = s_instance.lock();
		if (!r)
			s_instance = r = std::make_shared<DebuggerDetectionDisablerCreator>();
	}
	return r;
}

bool App::Misc::DebuggerDetectionDisabler::IsDebuggerActuallyPresent() const {
	return m_pImpl->IsDebuggerPresent.bridge();
}

std::weak_ptr<App::Misc::DebuggerDetectionDisabler> App::Misc::DebuggerDetectionDisabler::s_instance;

App::Misc::DebuggerDetectionDisabler::DebuggerDetectionDisabler()
	: m_pImpl(std::make_unique<Implementation>()) {
}

App::Misc::DebuggerDetectionDisabler::~DebuggerDetectionDisabler() = default;
