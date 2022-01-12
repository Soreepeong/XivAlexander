#include "pch.h"
#include "Misc/DebuggerDetectionDisabler.h"
#include "Misc/Hooks.h"

struct XivAlexander::Misc::DebuggerDetectionDisabler::Implementation {
	Hooks::PointerFunction<BOOL> IsDebuggerPresent{"DebuggerDetectionDisabler::IsDebuggerPresent", ::IsDebuggerPresent};
	Utils::CallOnDestruction::Multiple m_cleanup;

	Implementation() {
		Utils::Win32::DebugPrint(L"DebuggerDetectionDisabler: New");
		m_cleanup += IsDebuggerPresent.SetHook([]() { return FALSE; });
	}

	~Implementation() {
		Utils::Win32::DebugPrint(L"DebuggerDetectionDisabler: Destroy");
	}
};

class XivAlexander::Misc::DebuggerDetectionDisabler::DebuggerDetectionDisablerCreator : public DebuggerDetectionDisabler {
public:
	DebuggerDetectionDisablerCreator() = default;
	~DebuggerDetectionDisablerCreator() override = default;
};

std::shared_ptr<XivAlexander::Misc::DebuggerDetectionDisabler> XivAlexander::Misc::DebuggerDetectionDisabler::Acquire() {
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

bool XivAlexander::Misc::DebuggerDetectionDisabler::IsDebuggerActuallyPresent() const {
	return m_pImpl->IsDebuggerPresent.bridge();
}

std::weak_ptr<XivAlexander::Misc::DebuggerDetectionDisabler> XivAlexander::Misc::DebuggerDetectionDisabler::s_instance;

XivAlexander::Misc::DebuggerDetectionDisabler::DebuggerDetectionDisabler()
	: m_pImpl(std::make_unique<Implementation>()) {
}

XivAlexander::Misc::DebuggerDetectionDisabler::~DebuggerDetectionDisabler() = default;
