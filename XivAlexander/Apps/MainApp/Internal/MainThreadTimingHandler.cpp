#include "pch.h"

#include <XivAlexanderCommon/Utils/CallOnDestruction.h>
#include <XivAlexanderCommon/Utils/Signatures.h>

#include "Config.h"
#include "Apps/MainApp/App.h"
#include "Apps/MainApp/Internal/MainThreadTimingHandler.h"
#include "Apps/MainApp/Internal/NetworkTimingHandler.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Misc/Hooks.h"
#include "Misc/Logger.h"

static constexpr auto SecondToMicrosecondMultiplier = 1000000ULL;

struct XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler::Implementation {
	Apps::MainApp::App& App;
	const std::shared_ptr<Config> Config;
	const std::shared_ptr<Misc::Logger> Logger;

	std::optional<Misc::Hooks::PointerFunction<bool>> SingleMessageLoop;
	Misc::Hooks::ImportedFunction<BOOL, LPMSG, HWND, UINT, UINT, UINT> PeekMessageW{ "user32!PeekMessageW", "user32.dll", "PeekMessageW" };
	Misc::Hooks::ImportedFunction<void, DWORD> Sleep{ "kernel32!Sleep", "kernel32.dll", "Sleep" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, BOOL> SleepEx{ "kernel32!SleepEx", "kernel32.dll", "SleepEx" };

	std::deque<int64_t> LastMessagePumpCounterUs;
	std::set<int64_t> MessagePumpGuaranteeCounterUs;
	Utils::NumericStatisticsTracker MessagePumpIntervalTrackerUs{ 1024, 0 };

	Utils::CallOnDestruction::Multiple Cleanup;

	UINT LastPeekMessageHadRemoveMsg{};
	int64_t LastLockedFramerateRenderIntervalUs{};
	int64_t LastLockedFramerateRenderDriftUs{};

	Implementation(Apps::MainApp::App& app)
		: App(app)
		, Config(Config::Acquire())
		, Logger(Misc::Logger::Acquire()) {

		try {
			const auto base = reinterpret_cast<char*>(GetModuleHandleW(nullptr));
			const auto sizeOfImage = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew)->OptionalHeader.SizeOfImage;
			const auto mainModuleSpan = std::span<char>(base, sizeOfImage);
			const auto messageLoopSignature = Utils::Signatures::RegexSignature(R"(\xE8(....)\x84\xc0\x75\xf7)");
			Utils::Signatures::ScanResult sr;
			if (!messageLoopSignature.Lookup(mainModuleSpan, sr))
				throw std::runtime_error(__FUNCTION__ ": failed to find message loop");
			SingleMessageLoop.emplace("SingleMessageLoop", sr.ResolveAddress<bool(*)()>(1));
			Cleanup += SingleMessageLoop->SetHook([this]() { BeforePeekMessage(); return SingleMessageLoop->bridge(); });

		} catch (const std::exception& e) {
			Logger->Format<LogLevel::Info>(LogCategory::General, L"Hooking PeekMessageW instead of message loop: {}", e.what());

			Cleanup += PeekMessageW.SetHook([this](LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) mutable->BOOL {
				if (App.IsRunningOnGameMainThread()) {
					if (LastPeekMessageHadRemoveMsg == wRemoveMsg)
						BeforePeekMessage();
					else
						LastPeekMessageHadRemoveMsg = wRemoveMsg;
				}
				return PeekMessageW.bridge(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
				});
		}

		Cleanup += Sleep.SetHook([&](DWORD dwMilliseconds) {
			if (!ShouldSkipSleep(dwMilliseconds))
				Sleep.bridge(dwMilliseconds);
			});

		Cleanup += SleepEx.SetHook([&](DWORD dwMilliseconds, BOOL bAlertable) -> DWORD {
			// Note: if the user have conditional frame rate limit, then SleepEx(50) will be called.
			if (bAlertable || !ShouldSkipSleep(dwMilliseconds))
				return SleepEx.bridge(dwMilliseconds, bAlertable);

			return 0;
			});
	}

	~Implementation() {
		Cleanup.Clear();
	}

	bool ShouldSkipSleep(DWORD dwMilliseconds) const {
		static uint16_t s_counter = 0;
		if (dwMilliseconds > 1)
			return false;
		if (!Config->Runtime.UseMoreCpuTime)
			return false;
		if (GetForegroundWindow() != App.GetGameWindowHandle())
			return false;
		if (!++s_counter)
			SwitchToThread();
		return true;
	}

	void BeforePeekMessage() {
		const auto& rt = Config->Runtime;

		auto nowUs = Utils::QpcUs();

		while (!MessagePumpGuaranteeCounterUs.empty() && *MessagePumpGuaranteeCounterUs.begin() <= nowUs)
			MessagePumpGuaranteeCounterUs.erase(MessagePumpGuaranteeCounterUs.begin());

		int64_t waitUntilCounterUs = 0;
		if (!MessagePumpGuaranteeCounterUs.empty() && *MessagePumpGuaranteeCounterUs.begin() - nowUs <= MessagePumpIntervalTrackerUs.Latest()) {
			waitUntilCounterUs = *MessagePumpGuaranteeCounterUs.begin();
			MessagePumpGuaranteeCounterUs.erase(MessagePumpGuaranteeCounterUs.begin());
		}

		auto recordPumpInterval = false;
		if (!waitUntilCounterUs) {
			uint64_t waitForUs = 0, waitForDrift = 0;
			const auto test = Utils::QpcUs();
			if (const auto& networkingHelper = App.GetNetworkTimingHandler(); networkingHelper && rt.LockFramerateAutomatic) {
				const auto& group = networkingHelper->GetCooldownGroup(Apps::MainApp::Internal::NetworkTimingHandler::CooldownGroup::Id_Gcd);
				if (group.DurationUs != UINT64_MAX) {
					const auto frameInterval = static_cast<int64_t>(Config::RuntimeRepository::CalculateLockFramerateIntervalUs(
						rt.LockFramerateTargetFramerateRangeFrom,
						rt.LockFramerateTargetFramerateRangeTo,
						group.DurationUs,
						rt.LockFramerateMaximumRenderIntervalDeviation
					));
					if (frameInterval && LastLockedFramerateRenderIntervalUs && LastLockedFramerateRenderIntervalUs != frameInterval) {
						const auto prevRenderTimestamp = nowUs / LastLockedFramerateRenderIntervalUs * LastLockedFramerateRenderIntervalUs + LastLockedFramerateRenderDriftUs + frameInterval;
						int64_t minDiff = UINT64_MAX;
						int64_t minDriftUs = 0;
						for (int64_t i = 0; i < frameInterval; ++i) {
							const auto nextRenderTimestamp = static_cast<int64_t>((1 + (nowUs - i) / frameInterval) * frameInterval + i);
							if (nextRenderTimestamp < prevRenderTimestamp)
								continue;
							const auto diff = nextRenderTimestamp - prevRenderTimestamp;
							if (diff < minDiff) {
								minDiff = diff;
								minDriftUs = i;
							}
						}
						LastLockedFramerateRenderDriftUs = minDriftUs;
					}
					waitForUs = LastLockedFramerateRenderIntervalUs = frameInterval;
					waitForDrift = LastLockedFramerateRenderDriftUs;
				} else {
					waitForUs = LastLockedFramerateRenderIntervalUs = static_cast<uint64_t>(1000000. / std::min(1000000., std::max(1., rt.LockFramerateTargetFramerateRangeTo.Value())));
					waitForDrift = 0;
				}
			} else {
				waitForUs = rt.LockFramerateInterval;
				waitForDrift = 0;
			}
			if (waitForUs) {
				recordPumpInterval = true;
				waitUntilCounterUs = (1 + (nowUs - waitForDrift) / waitForUs) * waitForUs + waitForDrift;
			}
		}

		if (waitUntilCounterUs > 0 && !LastMessagePumpCounterUs.empty()) {
			const auto useMoreCpuTime = rt.UseMoreCpuTime.Value();
			while (waitUntilCounterUs > (nowUs = Utils::QpcUs())) {
				if (useMoreCpuTime)
					void(0);
				else
					::Sleep(0);
			}
			LastMessagePumpCounterUs.push_back(nowUs);
		} else {
			LastMessagePumpCounterUs.push_back(nowUs);
			recordPumpInterval = true;
		}
		if (recordPumpInterval && LastMessagePumpCounterUs.size() >= 2)
			MessagePumpIntervalTrackerUs.AddValue(LastMessagePumpCounterUs[LastMessagePumpCounterUs.size() - 1] - LastMessagePumpCounterUs[LastMessagePumpCounterUs.size() - 2]);
		if (LastMessagePumpCounterUs.size() > 2 && LastMessagePumpCounterUs.back() - LastMessagePumpCounterUs.front() >= SecondToMicrosecondMultiplier)
			LastMessagePumpCounterUs.pop_front();
	}
};

XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler::MainThreadTimingHandler(Apps::MainApp::App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {

}

const Utils::NumericStatisticsTracker& XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler::GetMessagePumpIntervalTrackerUs() const {
	return m_pImpl->MessagePumpIntervalTrackerUs;
}

void XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler::GuaranteePumpBeginCounterIn(int64_t nextInUs) {
	if (nextInUs > 0)
		m_pImpl->MessagePumpGuaranteeCounterUs.insert(Utils::QpcUs() + nextInUs);
}

void XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler::GuaranteePumpBeginCounterAt(int64_t counterUs) {
	m_pImpl->MessagePumpGuaranteeCounterUs.insert(counterUs);
}

XivAlexander::Apps::MainApp::Internal::MainThreadTimingHandler::~MainThreadTimingHandler() = default;
