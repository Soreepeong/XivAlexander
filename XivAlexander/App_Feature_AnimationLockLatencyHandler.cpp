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
		const std::shared_ptr<Config> Config;
		Implementation& Impl;
		Network::SingleConnection& Conn;

		struct PendingAction {
			uint32_t ActionId{};
			uint32_t Sequence{};
			int64_t RequestUs{};
			int64_t ResponseUs{};
			int64_t OriginalWaitUs{};
			int64_t WaitTimeUs{};
			bool CastFlag{};
		};

	public:
		// The game will allow the user to use an action, if server does not respond in 500ms since last action usage.
		// This will result in cancellation of following actions, so to prevent this, we keep track of outgoing action
		// request timestamps, and stack up required animation lock time responses from server.
		// The game will only process the latest animation lock duration information.
		std::deque<PendingAction> PendingActions;
		std::optional<PendingAction> LatestSuccessfulRequest;
		std::optional<int64_t> LastAnimationLockEndsAtUs;
		std::map<int, int64_t> OriginalWaitUsMap;
		Utils::NumericStatisticsTracker EarlyRequestsDurationUs{32, 0};


		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: Config(Config::Acquire())
			, Impl(*pImpl)
			, Conn(conn) {
			using namespace Network::Structures;

			const auto& gameConfig = Config->Game;
			const auto& runtimeConfig = Config->Runtime;

			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
					if (pMessage->Data.Ipc.SubType == gameConfig.C2S_ActionRequest[0]
						|| pMessage->Data.Ipc.SubType == gameConfig.C2S_ActionRequest[1]) {
						const auto& actionRequest = pMessage->Data.Ipc.Data.C2S_ActionRequest;
						PendingActions.emplace_back(PendingAction{
							.ActionId = actionRequest.ActionId,
							.Sequence = actionRequest.Sequence,
							.RequestUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier),
							});

						const auto delayUs = LastAnimationLockEndsAtUs ? PendingActions.back().RequestUs - *LastAnimationLockEndsAtUs : INT64_MAX;

						if (LastAnimationLockEndsAtUs && delayUs < 0) {
							if (runtimeConfig.UseEarlyPenalty) {
								// If somehow latest action request has been made before last animation lock end time,
								// penalize by forcing the next action to be usable after the early duration passes.
								LastAnimationLockEndsAtUs = *LastAnimationLockEndsAtUs - delayUs;

								// Record how early did the game let the user user action, and reflect that when deciding next extraDelay.
								EarlyRequestsDurationUs.AddValue(-delayUs);
							}

						} else {
							// Otherwise, if there was no action queued to begin with before the current one, update the base lock time to now.
							if (PendingActions.size() == 1)
								LastAnimationLockEndsAtUs = PendingActions.back().RequestUs;
						}

						if (runtimeConfig.UseHighLatencyMitigationLogging) {
							const auto prevRelativeUs = LatestSuccessfulRequest ? PendingActions.back().RequestUs - LatestSuccessfulRequest->RequestUs : INT64_MAX;

							Impl.m_logger->Format(
								LogCategory::AnimationLockLatencyHandler,
								"{:x}: C2S_ActionRequest({:04x}): actionId={:04x} sequence={:04x}{}{}",
								conn.Socket(),
								pMessage->Data.Ipc.SubType,
								actionRequest.ActionId,
								actionRequest.Sequence,
								delayUs > 10 * SecondToMicrosecondMultiplier ? "" : std::format(" delay={}s", static_cast<double>(delayUs) / SecondToMicrosecondMultiplier),
								prevRelativeUs > 10 * SecondToMicrosecondMultiplier ? "" : std::format(" prevRelative={}s", static_cast<double>(prevRelativeUs) / SecondToMicrosecondMultiplier));
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
						OriginalWaitUsMap[data.SourceSequence] = static_cast<uint64_t>(static_cast<double>(data.OriginalWaitTime) * SecondToMicrosecondMultiplier);
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

							if (const auto it = OriginalWaitUsMap.find(actionEffect.SourceSequence); it == OriginalWaitUsMap.end())
								waitUs = originalWaitUs = static_cast<int64_t>(static_cast<double>(actionEffect.AnimationLockDuration) * SecondToMicrosecondMultiplier);
							else {
								waitUs = originalWaitUs = it->second;
								OriginalWaitUsMap.erase(it);
							}

							if (actionEffect.SourceSequence == 0) {
								// Process actions originating from server.
								if (LatestSuccessfulRequest && !LatestSuccessfulRequest->CastFlag && LatestSuccessfulRequest->Sequence && *LastAnimationLockEndsAtUs > nowUs) {
									LatestSuccessfulRequest->ActionId = actionEffect.ActionId;
									LatestSuccessfulRequest->Sequence = 0;
									*LastAnimationLockEndsAtUs += (originalWaitUs + nowUs) - (LatestSuccessfulRequest->OriginalWaitUs + LatestSuccessfulRequest->ResponseUs);
									*LastAnimationLockEndsAtUs = std::max(*LastAnimationLockEndsAtUs, nowUs + AutoAttackDelayUs);
									waitUs = *LastAnimationLockEndsAtUs - nowUs;
								}
								description << " serverOriginated";

							} else {
								// find the one sharing Sequence, assuming action responses are always in order
								while (!PendingActions.empty() && PendingActions.front().Sequence != actionEffect.SourceSequence) {
									const auto& item = PendingActions.front();
									Impl.m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									PendingActions.pop_front();
								}

								if (!PendingActions.empty()) {
									LatestSuccessfulRequest = PendingActions.front();
									LatestSuccessfulRequest->ResponseUs = nowUs;
									LatestSuccessfulRequest->OriginalWaitUs = originalWaitUs;

									// 100ms animation lock after cast ends stays. Modify animation lock duration for instant actions only.
									// Since no other action is in progress right before the cast ends, we can safely replace the animation lock with the latest after-cast lock.
									if (!LatestSuccessfulRequest->CastFlag) {
										const auto rttUs = static_cast<int64_t>(nowUs - LatestSuccessfulRequest->RequestUs);
										conn.ApplicationLatencyUs.AddValue(rttUs);
										description << std::format(" rtt={}us", rttUs);
										LastAnimationLockEndsAtUs = ResolveNextAnimationLockEndUs(*LastAnimationLockEndsAtUs, nowUs, originalWaitUs, rttUs, description);
										waitUs = *LastAnimationLockEndsAtUs - nowUs;
									}
									PendingActions.pop_front();
								}
							}

							// Animation locks may not increase above twice of original.
							waitUs = std::min(originalWaitUs * 2, waitUs);
							LastAnimationLockEndsAtUs = nowUs + waitUs;

							if (waitUs < 0) {
								if (!runtimeConfig.UseHighLatencyMitigationPreviewMode) {
									actionEffect.AnimationLockDuration = 0;
									if (LatestSuccessfulRequest)
										LatestSuccessfulRequest->WaitTimeUs = -LatestSuccessfulRequest->OriginalWaitUs;
								}
								description << std::format(" wait={}us->{}us->0us (ping/jitter too high)",
									originalWaitUs, waitUs);
							} else if (waitUs < originalWaitUs) {
								if (!runtimeConfig.UseHighLatencyMitigationPreviewMode) {
									actionEffect.AnimationLockDuration = static_cast<float>(waitUs) / static_cast<float>(SecondToMicrosecondMultiplier);
									if (LatestSuccessfulRequest)
										LatestSuccessfulRequest->WaitTimeUs = waitUs - originalWaitUs;
									if (Config->Runtime.SynchronizeProcessing)
										XivAlexApp::GetCurrentApp()->GuaranteePumpBeginCounter(waitUs);
								}
								description << std::format(" wait={}us->{}us", originalWaitUs, waitUs);
							} else
								description << std::format(" wait={}us", originalWaitUs);
							description << std::format(" next={:%H:%M:%S}", std::chrono::system_clock::now() + std::chrono::milliseconds(waitUs));

							if (runtimeConfig.UseHighLatencyMitigationLogging)
								Impl.m_logger->Log(LogCategory::AnimationLockLatencyHandler, description.str());

						} else if (pMessage->Data.Ipc.SubType == gameConfig.S2C_ActorControlSelf) {
							auto& actorControlSelf = pMessage->Data.Ipc.Data.S2C_ActorControlSelf;

							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								// Received cooldown information; try to make the game accept input and process stuff as soon as cooldown expires
								const auto& cooldown = actorControlSelf.Cooldown;

								if (!PendingActions.empty() && PendingActions.front().ActionId == cooldown.ActionId) {
									const auto cooldownRegistrationDelayUs = Utils::GetHighPerformanceCounter(SecondToMicrosecondMultiplier) - PendingActions.front().RequestUs;
									if (Config->Runtime.SynchronizeProcessing)
										XivAlexApp::GetCurrentApp()->GuaranteePumpBeginCounter(cooldown.Duration * 10000LL - cooldownRegistrationDelayUs);

									if (runtimeConfig.UseHighLatencyMitigationLogging) {
										Impl.m_logger->Format(
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
								while (!PendingActions.empty()
									&& (
										// Sometimes SourceSequence is empty, in which case, we use ActionId to judge.
										(rollback.SourceSequence != 0 && PendingActions.front().Sequence != rollback.SourceSequence)
										|| (rollback.SourceSequence == 0 && PendingActions.front().ActionId != rollback.ActionId)
									)) {
									const auto& item = PendingActions.front();
									Impl.m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									PendingActions.pop_front();
								}

								if (!PendingActions.empty())
									PendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Impl.m_logger->Format(
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
								while (!PendingActions.empty() && PendingActions.front().ActionId != cancelCast.ActionId) {
									const auto& item = PendingActions.front();
									Impl.m_logger->Format(
										LogCategory::AnimationLockLatencyHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									PendingActions.pop_front();
								}

								if (!PendingActions.empty())
									PendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Impl.m_logger->Format(
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
							if (!PendingActions.empty())
								PendingActions.front().CastFlag = true;

							if (runtimeConfig.UseHighLatencyMitigationLogging)
								Impl.m_logger->Format(
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
			Conn.RemoveMessageHandlers(this);
		}

		int64_t ResolveNextAnimationLockEndUs(const int64_t lastAnimationLockEndsAtUs, const int64_t nowUs, const int64_t originalWaitUs, const int64_t rttUs, std::stringstream& description) {
			const auto& runtimeConfig = Config->Runtime;
			const auto pingTrackerUs = Conn.GetPingLatencyTrackerUs();

			const auto socketLatencyUs = Conn.FetchSocketLatencyUs();
			const auto pingLatencyUs = pingTrackerUs ? pingTrackerUs->Latest() : INT64_MAX;
			auto latencyUs = std::min(pingLatencyUs, socketLatencyUs);

			auto mode = runtimeConfig.HighLatencyMitigationMode.Value();

			if (latencyUs == INT64_MAX) {
				mode = HighLatencyMitigationMode::SimulateRtt;
				description << " latencyUnavailable";
			} else {
				if (latencyUs > rttUs)
					Conn.ExaggeratedNetworkLatencyUs.AddValue(latencyUs - rttUs);

				if (const auto exaggerationUs = Conn.ExaggeratedNetworkLatencyUs.Median();
					exaggerationUs != Conn.ExaggeratedNetworkLatencyUs.InvalidValue() && latencyUs >= exaggerationUs) {
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
					const auto rttMin = Conn.ApplicationLatencyUs.Min();
					const auto [rttMean, rttDeviation] = Conn.ApplicationLatencyUs.MeanAndDeviation();
					const auto [latencyMean, latencyDeviation] = !pingTrackerUs || pingLatencyUs > socketLatencyUs ? Conn.SocketLatencyUs.MeanAndDeviation() : pingTrackerUs->MeanAndDeviation();

					// Correct latency and server response time values in case of outliers.
					const auto latencyAdjustedImmediate = Utils::Clamp(latencyUs, latencyMean - latencyDeviation, latencyMean + latencyDeviation);
					const auto rttAdjusted = Utils::Clamp(rttUs, rttMean - rttDeviation, rttMean + rttDeviation);

					// Estimate latency based on server response time statistics.
					const auto latencyEstimate = (rttAdjusted + rttMin + rttMean) / 3 - rttDeviation;
					description << std::format(" latencyEstimate={}us", latencyEstimate);

					// Correct latency value based on estimate if server response time is stable.
					const auto latencyAdjusted = std::max(latencyEstimate, latencyAdjustedImmediate);

					const auto earlyPenalty = runtimeConfig.UseEarlyPenalty ? EarlyRequestsDurationUs.Max() : 0;
					if (earlyPenalty)
						description << std::format(" earlyPenalty={}us", earlyPenalty);

					// This delay is based on server's processing time.
					// If the server is busy, everyone should feel the same effect.
					// * Only the player's ping is taken out of the equation. (- latencyAdjusted)
					// * Prevent accidentally too high ExtraDelay. (Clamp above 1us)
					const auto delay = Utils::Clamp(rttAdjusted - latencyAdjusted + earlyPenalty, 1LL, MaximumExtraDelayUs);
					description << std::format(" delayAdjusted={}us", delay);

					if (rttUs > 100 && latencyUs < 5000) {
						Impl.m_logger->Format<LogLevel::Warning>(
							LogCategory::AnimationLockLatencyHandler,
							Config->Runtime.GetLangId(), IDS_WARNING_ZEROPING,
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
