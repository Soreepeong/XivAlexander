#include "pch.h"

#include <XivAlexanderCommon/Utils_CallOnDestruction.h>

#include "App_ConfigRepository.h"
#include "App_Feature_MainThreadTimingHandler.h"
#include "App_Feature_NetworkTimingHandler.h"
#include "App_Misc_Hooks.h"
#include "App_Network_SocketHook.h"
#include "App_XivAlexApp.h"

static constexpr auto SecondToMicrosecondMultiplier = 1000000ULL;

struct App::Feature::MainThreadTimingHandler::Implementation {
	XivAlexApp& App;
	const std::shared_ptr<Config> Config;

	Misc::Hooks::ImportedFunction<BOOL, LPMSG, HWND, UINT, UINT, UINT> PeekMessageW{ "user32!PeekMessageW", "user32.dll", "PeekMessageW" };
	Misc::Hooks::ImportedFunction<void, DWORD> Sleep{ "kernel32!Sleep", "kernel32.dll", "Sleep" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, BOOL> SleepEx{ "kernel32!SleepEx", "kernel32.dll", "SleepEx" };
	Misc::Hooks::ImportedFunction<DWORD, HANDLE, DWORD> WaitForSingleObject{ "kernel32!WaitForSingleObject", "kernel32.dll", "WaitForSingleObject" };
	Misc::Hooks::ImportedFunction<DWORD, HANDLE, DWORD, BOOL> WaitForSingleObjectEx{ "kernel32!WaitForSingleObjectEx", "kernel32.dll", "WaitForSingleObjectEx" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, const HANDLE*, BOOL, DWORD> WaitForMultipleObjects{ "kernel32!WaitForMultipleObjects", "kernel32.dll", "WaitForMultipleObjects" };
	Misc::Hooks::ImportedFunction<DWORD, DWORD, const HANDLE*, BOOL, DWORD, DWORD> MsgWaitForMultipleObjects{ "user32!MsgWaitForMultipleObjects", "user32.dll", "MsgWaitForMultipleObjects" };

	std::deque<int64_t> LastMessagePumpCounterUs;
	std::set<int64_t> MessagePumpGuaranteeCounterUs;
	int64_t LastWaitForSingleObjectUs{};
	Utils::NumericStatisticsTracker MessagePumpIntervalTrackerUs{ 1024, 0 };
	Utils::NumericStatisticsTracker RenderTimeTakenTrackerUs{ 1024, 0 };
	Utils::NumericStatisticsTracker SocketCallDelayTrackerUs{ 1024, 0 };

	Utils::CallOnDestruction::Multiple Cleanup;
	Utils::CallOnDestruction CleanupActionRequestListener;

	UINT LastPeekMessageHadRemoveMsg{};
	bool ExpectingAnyActionToBeUsedInRenderPass{};
	bool AnyActionUsedInRenderPass{};
	uint8_t NumberOfFramesToNonstopRender{};
	int64_t LastLockedFramerateRenderIntervalUs{};
	int64_t LastLockedFramerateRenderDriftUs{};

	Implementation(XivAlexApp& app)
		: App(app)
		, Config(Config::Acquire()) {

		if (const auto handler = App.GetNetworkTimingHandler())
			CleanupActionRequestListener = handler->OnActionRequestListener([&](auto&) { AnyActionUsedInRenderPass = true; });
		Cleanup += Config->Runtime.UseNetworkTimingHandler.OnChangeListener([&](auto&) {
			if (Config->Runtime.UseNetworkTimingHandler) {
				void(Utils::Win32::Thread(L"UseNetworkTimingHandler/OnCooldownGroupUpdateListener Waiter", [&]() {
					while (true) {
						if (const auto handler = App.GetNetworkTimingHandler()) {
							CleanupActionRequestListener = handler->OnActionRequestListener([&](auto&) { AnyActionUsedInRenderPass = true; });
							break;
						}
						Sleep(1);
					};
					}));
			} else {
				CleanupActionRequestListener.Clear();
			}
			});
		Cleanup += [&]() { CleanupActionRequestListener.Clear(); };

		Cleanup += PeekMessageW.SetHook([this](LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) mutable->BOOL {
			if (App.IsRunningOnGameMainThread()) {
				if (LastPeekMessageHadRemoveMsg == wRemoveMsg) {
					const auto& rt = Config->Runtime;

					auto nowUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier);

					if (LastWaitForSingleObjectUs && !LastMessagePumpCounterUs.empty()) {
						RenderTimeTakenTrackerUs.AddValue(LastWaitForSingleObjectUs - LastMessagePumpCounterUs.back());
						LastWaitForSingleObjectUs = 0;
					}

					// Make up for failed guarantees if any by rendering a frame without waiting
					while (!MessagePumpGuaranteeCounterUs.empty() && *MessagePumpGuaranteeCounterUs.begin() <= nowUs) {
						MessagePumpGuaranteeCounterUs.erase(MessagePumpGuaranteeCounterUs.begin());
						NumberOfFramesToNonstopRender = std::max<uint8_t>(NumberOfFramesToNonstopRender, 1);
					}

					// If we expected for something to happen but nothing happened, then render another without waiting for up to 3 frames
					if (AnyActionUsedInRenderPass)
						NumberOfFramesToNonstopRender = 0;
					else if (ExpectingAnyActionToBeUsedInRenderPass)
						NumberOfFramesToNonstopRender = std::max<uint8_t>(NumberOfFramesToNonstopRender, 10);
					else if (NumberOfFramesToNonstopRender)
						NumberOfFramesToNonstopRender--;
					ExpectingAnyActionToBeUsedInRenderPass = AnyActionUsedInRenderPass = false;

					if (NumberOfFramesToNonstopRender == 0) {
						int64_t waitUntilCounterUs = 0;
						if (!MessagePumpGuaranteeCounterUs.empty() && *MessagePumpGuaranteeCounterUs.begin() - nowUs <= MessagePumpIntervalTrackerUs.Latest()) {
							waitUntilCounterUs = *MessagePumpGuaranteeCounterUs.begin();
							MessagePumpGuaranteeCounterUs.erase(MessagePumpGuaranteeCounterUs.begin());
							ExpectingAnyActionToBeUsedInRenderPass = true;
						} else
							ExpectingAnyActionToBeUsedInRenderPass = false;

						auto recordPumpInterval = false;
						if (!waitUntilCounterUs) {
							uint64_t waitForUs = 0, waitForDrift = 0;
							const auto test = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier);
							if (const auto networkingHelper = App.GetNetworkTimingHandler(); networkingHelper && rt.LockFramerateAutomatic) {
								const auto& group = networkingHelper->GetCooldownGroup(Feature::NetworkTimingHandler::CooldownGroup::Id_Gcd);
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
									OutputDebugStringW(std::format(L"5. {}us / {} / {}\n", Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier) - test, waitForUs, waitForDrift).c_str());
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

						if (!LastMessagePumpCounterUs.empty()) {
							if (const auto socketSelectUs = App.GetSocketHook() ? App.GetSocketHook()->GetLastSocketSelectCounterUs() : 0)
								SocketCallDelayTrackerUs.AddValue(socketSelectUs - LastMessagePumpCounterUs.back());
						}

						if (waitUntilCounterUs > 0 && !LastMessagePumpCounterUs.empty()) {
							const auto useMoreCpuTime = rt.UseMoreCpuTime.Value();
							while (waitUntilCounterUs > (nowUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier))) {
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

					} else {
						LastMessagePumpCounterUs.push_back(nowUs);
					}

				} else {
					LastPeekMessageHadRemoveMsg = wRemoveMsg;
				}
			}
			return PeekMessageW.bridge(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
			});

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

		Cleanup += WaitForSingleObject.SetHook([&](HANDLE hHandle, DWORD dwMilliseconds) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			else if (dwMilliseconds == INFINITE && App.IsRunningOnGameMainThread())
				LastWaitForSingleObjectUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier);
			return WaitForSingleObject.bridge(hHandle, dwMilliseconds);
			});

		Cleanup += WaitForSingleObjectEx.SetHook([&](HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			return WaitForSingleObjectEx.bridge(hHandle, dwMilliseconds, bAlertable);
			});

		Cleanup += WaitForMultipleObjects.SetHook([&](DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			return WaitForMultipleObjects.bridge(nCount, lpHandles, bWaitAll, dwMilliseconds);
			});

		Cleanup += MsgWaitForMultipleObjects.SetHook([&](DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask) -> DWORD {
			if (ShouldSkipSleep(dwMilliseconds))
				dwMilliseconds = 0;
			return MsgWaitForMultipleObjects.bridge(nCount, lpHandles, bWaitAll, dwMilliseconds, dwWakeMask);
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
};

App::Feature::MainThreadTimingHandler::MainThreadTimingHandler(XivAlexApp& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {

}

const Utils::NumericStatisticsTracker& App::Feature::MainThreadTimingHandler::GetMessagePumpIntervalTrackerUs() const {
	return m_pImpl->MessagePumpIntervalTrackerUs;
}

const Utils::NumericStatisticsTracker& App::Feature::MainThreadTimingHandler::GetRenderTimeTakenTrackerUs() const {
	return m_pImpl->RenderTimeTakenTrackerUs;
}

const Utils::NumericStatisticsTracker& App::Feature::MainThreadTimingHandler::GetSocketCallDelayTrackerUs() const {
	return m_pImpl->SocketCallDelayTrackerUs;
}

void App::Feature::MainThreadTimingHandler::GuaranteePumpBeginCounterIn(int64_t nextInUs) {
	if (nextInUs > 0)
		m_pImpl->MessagePumpGuaranteeCounterUs.insert(Utils::GetHighPerformanceCounter(1000000) + nextInUs);
}

void App::Feature::MainThreadTimingHandler::GuaranteePumpBeginCounterAt(int64_t counterUs) {

}

App::Feature::MainThreadTimingHandler::~MainThreadTimingHandler() = default;
