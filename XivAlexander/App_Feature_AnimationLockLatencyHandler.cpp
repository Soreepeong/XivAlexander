#include "pch.h"
#include "App_Feature_AnimationLockLatencyHandler.h"

#include <XivAlexanderCommon/XaMisc.h>

#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"
#include "App_XivAlexApp.h"
#include "resource.h"

struct App::Feature::AnimationLockLatencyHandler::Implementation {
	// Server responses have been usually taking between 50ms and 100ms on below-1ms
	// latency to server, so 75ms is a good average.
	// The server will do sanity check on the frequency of action use requests,
	// and it's very easy to identify whether you're trying to go below allowed minimum value.
	// This addon is already in gray area. Do NOT decrease this value. You've been warned.
	// Feel free to increase and see how does it feel like to play on high latency instead, though.
	static constexpr int64_t DefaultDelayUs = 75000;

	// On unstable network connection, limit the possible overshoot in ExtraDelay.
	static constexpr int64_t MaximumExtraDelayUs = 150000;

	static constexpr int64_t AutoAttackDelayUs = 100000;

	static constexpr auto SecondToMicrosecondMultiplier = 1000000;

	class SingleConnectionHandler {

		const std::shared_ptr<Config> m_config;

		struct PendingAction {
			uint32_t ActionId;
			uint32_t Sequence;
			int64_t RequestUs;
			int64_t ResponseUs = 0;
			bool CastFlag = false;
			int64_t OriginalWaitUs = 0;
			int64_t WaitTimeUs = 0;

			PendingAction()
				: ActionId(0)
				, Sequence(0)
				, RequestUs(0) {
			}

			explicit PendingAction(const Network::Structures::XivIpcs::C2S_ActionRequest& request)
				: ActionId(request.ActionId)
				, Sequence(request.Sequence)
				, RequestUs(Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier)) {
			}
		};

	public:
		// The game will allow the user to use an action, if server does not respond in 500ms since last action usage.
		// This will result in cancellation of following actions, so to prevent this, we keep track of outgoing action
		// request timestamps, and stack up required animation lock time responses from server.
		// The game will only process the latest animation lock duration information.
		std::deque<PendingAction> m_pendingActions{};
		PendingAction m_latestSuccessfulRequest;
		int64_t m_lastAnimationLockEndsAtUs = 0;
		std::map<int, int64_t> m_originalWaitUsMap{};
		Utils::NumericStatisticsTracker m_earlyRequestsDurationUs{32, 0};

		Implementation* m_pImpl;
		Network::SingleConnection& conn;

		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: m_config(Config::Acquire())
			, m_pImpl(pImpl)
			, conn(conn) {
			using namespace Network::Structures;

			const auto& gameConfig = m_config->Game;
			const auto& runtimeConfig = m_config->Runtime;

			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
					if (pMessage->Data.Ipc.SubType == gameConfig.C2S_ActionRequest[0]
						|| pMessage->Data.Ipc.SubType == gameConfig.C2S_ActionRequest[1]) {
						const auto& actionRequest = pMessage->Data.Ipc.Data.C2S_ActionRequest;
						m_pendingActions.emplace_back(actionRequest);

						const auto delayUs = m_pendingActions.back().RequestUs - m_lastAnimationLockEndsAtUs;

						if (delayUs < 0) {
							if (runtimeConfig.UseEarlyPenalty) {
								// If somehow latest action request has been made before last animation lock end time,
								// penalize by forcing the next action to be usable after the early duration passes.
								m_lastAnimationLockEndsAtUs -= delayUs;

								// Record how early did the game let the user user action, and reflect that when deciding next extraDelay.
								m_earlyRequestsDurationUs.AddValue(-delayUs);
							}

						} else {
							// Otherwise, if there was no action queued to begin with before the current one, update the base lock time to now.
							if (m_pendingActions.size() == 1)
								m_lastAnimationLockEndsAtUs = m_pendingActions.back().RequestUs;
						}

						if (runtimeConfig.UseHighLatencyMitigationLogging) {
							const auto prevRelativeUs = m_pendingActions.back().RequestUs - m_latestSuccessfulRequest.RequestUs;

							m_pImpl->m_logger->Format(
								LogCategory::AnimationLockLatencyHandler,
								"{:x}: C2S_ActionRequest({:04x}): actionId={:04x} sequence={:04x}{}{}",
								conn.Socket(),
								pMessage->Data.Ipc.SubType,
								actionRequest.ActionId,
								actionRequest.Sequence,
								delayUs > 10 * SecondToMicrosecondMultiplier ? "" : std::format(
									" delay={}{}.{:06}s",
									delayUs > 0 ? "" : "-", abs(delayUs) / SecondToMicrosecondMultiplier, abs(delayUs) % SecondToMicrosecondMultiplier
								),
								prevRelativeUs > 10 * SecondToMicrosecondMultiplier ? "" : std::format(
									" prevRelative={}.{:06}s",
									prevRelativeUs / SecondToMicrosecondMultiplier, prevRelativeUs % SecondToMicrosecondMultiplier
								));
						}
					}
				}
				return true;
			});
			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				const auto nowUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier);

				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::CustomType) {
					if (pMessage->Data.Ipc.SubType == static_cast<uint16_t>(IpcCustomSubtype::OriginalWaitTime)) {
						const auto& data = pMessage->Data.Ipc.Data.S2C_Custom_OriginalWaitTime;
						m_originalWaitUsMap[data.SourceSequence] = static_cast<uint64_t>(static_cast<double>(data.OriginalWaitTime) * SecondToMicrosecondMultiplier);
					}

					// Don't relay custom Ipc data to game.
					return false;

				} else if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
					// Only interested in messages intended for the current player
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (gameConfig.S2C_ActionEffects[0] == pMessage->Data.Ipc.SubType
							|| gameConfig.S2C_ActionEffects[1] == pMessage->Data.Ipc.SubType
							|| gameConfig.S2C_ActionEffects[2] == pMessage->Data.Ipc.SubType
							|| gameConfig.S2C_ActionEffects[3] == pMessage->Data.Ipc.SubType
							|| gameConfig.S2C_ActionEffects[4] == pMessage->Data.Ipc.SubType) {

							// actionEffect has to be modified later on, so no const
							auto& actionEffect = pMessage->Data.Ipc.Data.S2C_ActionEffect;
							int64_t originalWaitUs, waitUs;

							std::stringstream description;
							description << std::format("{:x}: S2C_ActionEffect({:04x}): actionId={:04x} sourceSequence={:04x}",
								conn.Socket(),
								pMessage->Data.Ipc.SubType,
								actionEffect.ActionId,
								actionEffect.SourceSequence);

							if (const auto it = m_originalWaitUsMap.find(actionEffect.SourceSequence); it == m_originalWaitUsMap.end())
								waitUs = originalWaitUs = static_cast<int64_t>(static_cast<double>(actionEffect.AnimationLockDuration) * SecondToMicrosecondMultiplier);
							else {
								waitUs = originalWaitUs = it->second;
								m_originalWaitUsMap.erase(it);
							}

							if (actionEffect.SourceSequence == 0) {
								// Process actions originating from server.
								if (!m_latestSuccessfulRequest.CastFlag && m_latestSuccessfulRequest.Sequence && m_lastAnimationLockEndsAtUs > nowUs) {
									m_latestSuccessfulRequest.ActionId = actionEffect.ActionId;
									m_latestSuccessfulRequest.Sequence = 0;
									m_lastAnimationLockEndsAtUs += (originalWaitUs + nowUs) - (m_latestSuccessfulRequest.OriginalWaitUs + m_latestSuccessfulRequest.ResponseUs);
									m_lastAnimationLockEndsAtUs = std::max(m_lastAnimationLockEndsAtUs, nowUs + AutoAttackDelayUs);
									waitUs = m_lastAnimationLockEndsAtUs - nowUs;
								}
								description << " serverOriginated";

							} else {
								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() && m_pendingActions.front().Sequence != actionEffect.SourceSequence) {
									const auto& item = m_pendingActions.front();
									m_pImpl->m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty()) {
									m_latestSuccessfulRequest = m_pendingActions.front();
									m_latestSuccessfulRequest.ResponseUs = nowUs;
									m_latestSuccessfulRequest.OriginalWaitUs = originalWaitUs;

									// 100ms animation lock after cast ends stays. Modify animation lock duration for instant actions only.
									// Since no other action is in progress right before the cast ends, we can safely replace the animation lock with the latest after-cast lock.
									if (!m_latestSuccessfulRequest.CastFlag) {
										const auto rttUs = static_cast<int64_t>(nowUs - m_latestSuccessfulRequest.RequestUs);
										conn.ApplicationLatencyUs.AddValue(rttUs);
										description << std::format(" rtt={}us", rttUs);
										m_lastAnimationLockEndsAtUs = ResolveNextAnimationLockEndUs(m_lastAnimationLockEndsAtUs, nowUs, originalWaitUs, rttUs, description);
										waitUs = m_lastAnimationLockEndsAtUs - nowUs;
									}
									m_pendingActions.pop_front();
								}
							}

							if (waitUs < 0) {
								if (!runtimeConfig.UseHighLatencyMitigationPreviewMode) {
									actionEffect.AnimationLockDuration = 0;
									m_latestSuccessfulRequest.WaitTimeUs = -m_latestSuccessfulRequest.OriginalWaitUs;
								}
								description << std::format(" wait={}us->{}us->0us (ping/jitter too high)",
									originalWaitUs, waitUs);
							} else if (waitUs != originalWaitUs) {
								if (!runtimeConfig.UseHighLatencyMitigationPreviewMode) {
									actionEffect.AnimationLockDuration = static_cast<float>(waitUs) / 1000000.f;
									m_latestSuccessfulRequest.WaitTimeUs = waitUs - originalWaitUs;
									XivAlexApp::GetCurrentApp()->GuaranteePumpBeginCounter(waitUs);
								}
								description << std::format(" wait={}us->{}us", originalWaitUs, waitUs);
							} else
								description << std::format(" wait={}us", originalWaitUs);
							description << std::format(" next={:%H:%M:%S}", std::chrono::system_clock::now() + std::chrono::milliseconds(waitUs));
							if (runtimeConfig.UseHighLatencyMitigationLogging)
								m_pImpl->m_logger->Log(LogCategory::AnimationLockLatencyHandler, description.str());

						} else if (pMessage->Data.Ipc.SubType == gameConfig.S2C_ActorControlSelf) {
							auto& actorControlSelf = pMessage->Data.Ipc.Data.S2C_ActorControlSelf;

							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								// Received cooldown information; try to make the game accept input and process stuff as soon as cooldown expires
								const auto& cooldown = actorControlSelf.Cooldown;

								if (!m_pendingActions.empty() && m_pendingActions.front().ActionId == cooldown.ActionId) {
									const auto cooldownRegistrationDelayUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier) - m_pendingActions.front().RequestUs;
									XivAlexApp::GetCurrentApp()->GuaranteePumpBeginCounter(cooldown.Duration * 10000LL - cooldownRegistrationDelayUs);

									if (runtimeConfig.UseHighLatencyMitigationLogging) {
										m_pImpl->m_logger->Format(
											LogCategory::AnimationLockLatencyHandler,
											"{:x}: S2C_ActorControlSelf/Cooldown: actionId={:04x} duration={}.{:02}s",
											conn.Socket(),
											cooldown.ActionId,
											cooldown.Duration / 100, cooldown.Duration % 100);
									}
								}

							} else if (actorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								// Oldest action request has been rejected from server.
								const auto& rollback = actorControlSelf.Rollback;

								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty()
									&& (
										// Sometimes SourceSequence is empty, in which case, we use ActionId to judge.
										(rollback.SourceSequence != 0 && m_pendingActions.front().Sequence != rollback.SourceSequence)
										|| (rollback.SourceSequence == 0 && m_pendingActions.front().ActionId != rollback.ActionId)
									)) {
									const auto& item = m_pendingActions.front();
									m_pImpl->m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									m_pImpl->m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										"{:x}: S2C_ActorControlSelf/ActionRejected: actionId={:04x} sourceSequence={:04x}",
										conn.Socket(),
										rollback.ActionId,
										rollback.SourceSequence);
							}

						} else if (pMessage->Data.Ipc.SubType == gameConfig.S2C_ActorControl) {
							const auto& actorControl = pMessage->Data.Ipc.Data.S2C_ActorControl;

							// The server has cancelled an oldest action (which is a cast) in progress.
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;

								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() && m_pendingActions.front().ActionId != cancelCast.ActionId) {
									const auto& item = m_pendingActions.front();
									m_pImpl->m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									m_pImpl->m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										"{:x}: S2C_ActorControl/CancelCast: actionId={:04x}",
										conn.Socket(),
										cancelCast.ActionId);
							}

						} else if (pMessage->Data.Ipc.SubType == gameConfig.S2C_ActorCast) {
							const auto& actorCast = pMessage->Data.Ipc.Data.S2C_ActorCast;
							// Mark that the last request was a cast.
							// If it indeed is a cast, the game UI will block the user from generating additional requests,
							// so first item is guaranteed to be the cast action.
							if (!m_pendingActions.empty())
								m_pendingActions.front().CastFlag = true;

							if (runtimeConfig.UseHighLatencyMitigationLogging)
								m_pImpl->m_logger->Format(
									LogCategory::AnimationLockLatencyHandler,
									"{:x}: S2C_ActorCast: actionId={:04x} time={:.3f} target={:08x}",
									conn.Socket(),
									actorCast.ActionId,
									actorCast.CastTime,
									actorCast.TargetId);
						}
					}
				}
				return true;
			});
		}

		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
		}

		int64_t ResolveNextAnimationLockEndUs(const int64_t lastAnimationLockEndsAtUs, const int64_t nowUs, const int64_t originalWaitUs, const int64_t rttUs, std::stringstream& description) {
			const auto& runtimeConfig = m_config->Runtime;
			const auto pingTrackerUs = conn.GetPingLatencyTrackerUs();

			const auto socketLatencyUs = conn.FetchSocketLatencyUs();
			const auto pingLatencyUs = pingTrackerUs ? pingTrackerUs->Latest() : INT64_MAX;
			auto latencyUs = std::min(pingLatencyUs, socketLatencyUs);

			auto mode = runtimeConfig.HighLatencyMitigationMode.Value();

			if (latencyUs == INT64_MAX) {
				mode = HighLatencyMitigationMode::SimulateRtt;
				description << " latencyUnavailable";
			} else {
				if (latencyUs > rttUs)
					conn.ExaggeratedNetworkLatencyUs.AddValue(latencyUs - rttUs);

				if (const auto exaggerationUs = conn.ExaggeratedNetworkLatencyUs.Median();
					exaggerationUs != conn.ExaggeratedNetworkLatencyUs.InvalidValue() && latencyUs >= exaggerationUs) {
					// Reported latency is higher than rtt, which means latency measurement is unreliable.
					if (socketLatencyUs < pingLatencyUs)
						description << std::format(" socketLatency={}->{}us", latencyUs, latencyUs - exaggerationUs);
					else
						description << std::format(" pingLatency={}->{}us", latencyUs, latencyUs - exaggerationUs);
					latencyUs -= exaggerationUs;
				} else {
					if (socketLatencyUs < pingLatencyUs)
						description << std::format(" pingLatency={}us", latencyUs);
					else
						description << std::format(" socketLatency={}us", latencyUs);
				}
			}

			switch (mode) {
				case HighLatencyMitigationMode::SubtractLatency:
					description << std::format(" delay={}us", DefaultDelayUs);
					return nowUs + originalWaitUs - latencyUs;

				case HighLatencyMitigationMode::SimulateNormalizedRttAndLatency: {
					const auto rttMin = conn.ApplicationLatencyUs.Min();
					const auto rttMean = conn.ApplicationLatencyUs.Mean();
					const auto rttDeviation = conn.ApplicationLatencyUs.Deviation();
					const auto latencyMean = !pingTrackerUs || pingLatencyUs > socketLatencyUs ? conn.SocketLatencyUs.Mean() : pingTrackerUs->Mean();
					const auto latencyDeviation = !pingTrackerUs || pingLatencyUs > socketLatencyUs ? conn.SocketLatencyUs.Deviation() : pingTrackerUs->Deviation();

					// Correct latency and server response time values in case of outliers.
					const auto latencyAdjustedImmediate = Utils::Clamp(latencyUs, latencyMean - latencyDeviation, latencyMean + latencyDeviation);
					const auto rttAdjusted = Utils::Clamp(rttUs, rttMean - rttDeviation, rttMean + rttDeviation);

					// Estimate latency based on server response time statistics.
					const auto latencyEstimate = (rttAdjusted + rttMin + rttMean) / 3 - rttDeviation;
					description << std::format(" latencyEstimate={}us", latencyEstimate);

					// Correct latency value based on estimate if server response time is stable.
					const auto latencyAdjusted = std::max(latencyEstimate, latencyAdjustedImmediate);

					const auto earlyPenalty = runtimeConfig.UseEarlyPenalty ? m_earlyRequestsDurationUs.Max() : 0;
					if (earlyPenalty)
						description << std::format(" earlyPenalty={}us", earlyPenalty);

					// This delay is based on server's processing time.
					// If the server is busy, everyone should feel the same effect.
					// * Only the player's ping is taken out of the equation. (- latencyAdjusted)
					// * Prevent accidentally too high ExtraDelay. (Clamp above 1us)
					const auto delay = Utils::Clamp(rttAdjusted - latencyAdjusted + earlyPenalty, 1LL, MaximumExtraDelayUs);
					description << std::format(" delayAdjusted={}us", delay);

					if (rttUs > 100 && latencyUs < 5000) {
						m_pImpl->m_logger->Format<LogLevel::Warning>(
							LogCategory::AnimationLockLatencyHandler,
							m_config->Runtime.GetLangId(), IDS_WARNING_ZEROPING,
							rttUs, latencyUs / 1000);
					}
					return lastAnimationLockEndsAtUs + originalWaitUs + delay;
				}
			}

			description << std::format(" delay={}us", DefaultDelayUs);
			return lastAnimationLockEndsAtUs + originalWaitUs + DefaultDelayUs;
		}
	};

	const std::shared_ptr<Misc::Logger> m_logger;
	Network::SocketHook* const m_socketHook;
	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers{};
	Utils::CallOnDestruction::Multiple m_cleanup;

	Implementation(Network::SocketHook* socketHook)
		: m_logger(Misc::Logger::Acquire())
		, m_socketHook(socketHook) {
		m_cleanup += m_socketHook->OnSocketFound([&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(this, conn));
		});
		m_cleanup += m_socketHook->OnSocketGone([&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
		});
	}

	~Implementation() {
		m_handlers.clear();
	}
};

App::Feature::AnimationLockLatencyHandler::AnimationLockLatencyHandler(Network::SocketHook* socketHook)
	: m_pImpl(std::make_unique<Implementation>(socketHook)) {
}

App::Feature::AnimationLockLatencyHandler::~AnimationLockLatencyHandler() = default;
