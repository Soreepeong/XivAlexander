#include "pch.h"
#include "Apps/MainApp/Internal/NetworkTimingHandler.h"

#include <XivAlexanderCommon/Sqex/Network/Structure.h>

#include "Apps/MainApp/App.h"
#include "Apps/MainApp/Internal/MainThreadTimingHandler.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Config.h"
#include "Misc/Logger.h"
#include "resource.h"

using namespace Sqex::Network::Structure;

struct XivAlexander::Apps::MainApp::Internal::NetworkTimingHandler::Implementation {
	static constexpr int64_t AutoAttackDelayUs = 100000;

	static constexpr auto SecondToMicrosecondMultiplier = 1000000;

	std::map<uint32_t, CooldownGroup> LastCooldownGroup;

	class SingleConnectionHandler {
		const std::shared_ptr<Config> Config;
		Implementation& Impl;
		SingleConnection& Conn;

		struct PendingAction {
			uint32_t ActionId{};
			uint32_t Sequence{};
			int64_t RequestUs{};
			int64_t ResponseUs{};
			int64_t OriginalWaitUs{};
			int64_t WaitTimeUs{};
			int64_t CastTimeUs{};
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

		SingleConnectionHandler(Implementation* pImpl, SingleConnection& conn)
			: Config(Config::Acquire())
			, Impl(*pImpl)
			, Conn(conn) {

			const auto& gameConfig = Config->Game;
			const auto& runtimeConfig = Config->Runtime;

			Impl.LastCooldownGroup.clear();

			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
					if (pMessage->Data.Ipc.SubType == gameConfig.C2S_ActionRequest[0]
						|| pMessage->Data.Ipc.SubType == gameConfig.C2S_ActionRequest[1]) {
						const auto& actionRequest = pMessage->Data.Ipc.Data.C2S_ActionRequest;
						Impl.CallOnActionRequestListener(actionRequest);
						PendingActions.emplace_back(PendingAction{
							.ActionId = actionRequest.ActionId,
							.Sequence = actionRequest.Sequence,
							.RequestUs = Utils::QpcUs(),
							});

						if (runtimeConfig.UseHighLatencyMitigationLogging) {
							const auto delayUs = LastAnimationLockEndsAtUs ? PendingActions.back().RequestUs - *LastAnimationLockEndsAtUs : INT64_MAX;
							const auto prevRelativeUs = LatestSuccessfulRequest ? PendingActions.back().RequestUs - LatestSuccessfulRequest->RequestUs : INT64_MAX;

							Impl.Logger->Format(
								LogCategory::NetworkTimingHandler,
								"{:x}: C2S_ActionRequest({:04x}): actionId={:04x} sequence={:04x}{}{}",
								conn.Socket(),
								pMessage->Data.Ipc.SubType,
								actionRequest.ActionId,
								actionRequest.Sequence,
								delayUs > 10 * SecondToMicrosecondMultiplier ? "" : std::format(" delay={}s", static_cast<double>(delayUs) / SecondToMicrosecondMultiplier),
								prevRelativeUs > 10 * SecondToMicrosecondMultiplier ? "" : std::format(" prevRelative={}s", static_cast<double>(prevRelativeUs) / SecondToMicrosecondMultiplier));
						}

						// If there was no action queued to begin with before the current one, update the base lock time to now.
						if (PendingActions.size() == 1 && (!PendingActions.back().RequestUs || *LastAnimationLockEndsAtUs < PendingActions.back().RequestUs))
							LastAnimationLockEndsAtUs = PendingActions.back().RequestUs;
					}
				}
				return true;
				});
			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				const auto nowUs = Utils::QpcUs();

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
								waitUs = originalWaitUs = actionEffect.AnimationLockDurationUs();
							else {
								waitUs = originalWaitUs = it->second;
								OriginalWaitUsMap.erase(it);
							}

							if (actionEffect.SourceSequence == 0) {
								// Process actions originating from server.
								if (LatestSuccessfulRequest && !LatestSuccessfulRequest->CastTimeUs && LatestSuccessfulRequest->Sequence) {
									LatestSuccessfulRequest->ActionId = actionEffect.ActionId;
									LatestSuccessfulRequest->Sequence = 0;
									*LastAnimationLockEndsAtUs += (originalWaitUs + nowUs) - (LatestSuccessfulRequest->OriginalWaitUs + LatestSuccessfulRequest->ResponseUs);
									LastAnimationLockEndsAtUs = Utils::Clamp(*LastAnimationLockEndsAtUs, nowUs + AutoAttackDelayUs, nowUs + AutoAttackDelayUs + originalWaitUs);

								} else {
									LastAnimationLockEndsAtUs = nowUs + waitUs;
								}
								description << " serverOriginated";

							} else {
								// find the one sharing Sequence, assuming action responses are always in order
								while (!PendingActions.empty() && PendingActions.front().Sequence != actionEffect.SourceSequence) {
									const auto& item = PendingActions.front();
									Impl.Logger->Format(
										LogCategory::NetworkTimingHandler,
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
									if (!LatestSuccessfulRequest->CastTimeUs) {
										const auto rttUs = static_cast<int64_t>(nowUs - LatestSuccessfulRequest->RequestUs);
										conn.ApplicationLatencyUs.AddValue(rttUs);
										description << std::format(" rtt={}us", rttUs);
										LastAnimationLockEndsAtUs = ResolveNextAnimationLockEndUs(*LastAnimationLockEndsAtUs, nowUs, originalWaitUs, rttUs, description);

									} else {
										LastAnimationLockEndsAtUs = LatestSuccessfulRequest->RequestUs + LatestSuccessfulRequest->CastTimeUs + waitUs;
									}
									PendingActions.pop_front();

								} else {
									LastAnimationLockEndsAtUs = nowUs + waitUs;
								}
							}

							waitUs = *LastAnimationLockEndsAtUs - nowUs;
							if (waitUs == originalWaitUs || (LatestSuccessfulRequest && LatestSuccessfulRequest->CastTimeUs)) {
								description << std::format(" wait={}us", originalWaitUs);
							} else if (waitUs < 0) {
								const auto invalidWaitUs = waitUs;
								waitUs = 0;
								description << std::format(" wait={}us->{}us->{}us (ping/jitter too high)", originalWaitUs, invalidWaitUs, waitUs);

								if (!runtimeConfig.UseHighLatencyMitigationPreviewMode) {
									actionEffect.AnimationLockDurationUs(0);
									if (LatestSuccessfulRequest)
										LatestSuccessfulRequest->WaitTimeUs = -LatestSuccessfulRequest->OriginalWaitUs;
								}

							} else if (waitUs < originalWaitUs) {
								description << std::format(" wait={}us->{}us", originalWaitUs, waitUs);

								if (!runtimeConfig.UseHighLatencyMitigationPreviewMode) {
									actionEffect.AnimationLockDurationUs(waitUs);
									if (LatestSuccessfulRequest)
										LatestSuccessfulRequest->WaitTimeUs = waitUs - originalWaitUs;
								}

							}
							description << std::format(" next={:%H:%M:%S}", std::chrono::system_clock::now() + std::chrono::microseconds(waitUs));

							if (Config->Runtime.SynchronizeProcessing) {
								if (auto& handler = Impl.App.GetMainThreadTimingHelper()) {
									handler->GuaranteePumpBeginCounterAt(*LastAnimationLockEndsAtUs + (LatestSuccessfulRequest ? LatestSuccessfulRequest->CastTimeUs : 0));
								}
							}

							if (runtimeConfig.UseHighLatencyMitigationLogging)
								Impl.Logger->Log(LogCategory::NetworkTimingHandler, description.str());

						} else if (pMessage->Data.Ipc.SubType == gameConfig.S2C_ActorControlSelf) {
							auto& actorControlSelf = pMessage->Data.Ipc.Data.S2C_ActorControlSelf;

							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								// Received cooldown information; try to make the game accept input and process stuff as soon as cooldown expires
								const auto& cooldown = actorControlSelf.Cooldown;
								auto& group = Impl.LastCooldownGroup[cooldown.CooldownGroupId];
								auto newDriftItem = false;
								group.Id = cooldown.CooldownGroupId;

								if (!PendingActions.empty() && PendingActions.front().ActionId == cooldown.ActionId) {
									if (group.DurationUs != UINT64_MAX && group.TimestampUs && PendingActions.front().RequestUs - group.TimestampUs > 0 && PendingActions.front().RequestUs - group.TimestampUs < group.DurationUs * 2) {
										group.DriftTrackerUs.AddValue(PendingActions.front().RequestUs - group.TimestampUs - group.DurationUs);
										newDriftItem = true;
									}
									group.TimestampUs = PendingActions.front().RequestUs;

									if (Config->Runtime.SynchronizeProcessing) {
										if (group.Id != CooldownGroup::Id_Gcd || !(Config->Runtime.LockFramerateAutomatic || Config->Runtime.LockFramerateInterval)) {
											if (auto& handler = Impl.App.GetMainThreadTimingHelper())
												handler->GuaranteePumpBeginCounterAt(PendingActions.front().RequestUs + cooldown.DurationUs());
										}
									}

									if (runtimeConfig.UseHighLatencyMitigationLogging) {
										Impl.Logger->Format(
											LogCategory::NetworkTimingHandler,
											"{:x}: S2C_ActorControlSelf/Cooldown: actionId={:04x} group={:04x} duration={:.02f}s",
											conn.Socket(),
											cooldown.ActionId, cooldown.CooldownGroupId,
											cooldown.DurationF());
									}
								}

								group.DurationUs = cooldown.DurationUs();
								Impl.CallOnCooldownGroupUpdateListener(group.Id, newDriftItem);

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
									Impl.Logger->Format(
										LogCategory::NetworkTimingHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									PendingActions.pop_front();
								}

								if (!PendingActions.empty())
									PendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Impl.Logger->Format(
										LogCategory::NetworkTimingHandler,
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
									Impl.Logger->Format(
										LogCategory::NetworkTimingHandler,
										u8"\t┎ ActionRequest ignored for processing: actionId={:04x} sequence={:04x}",
										item.ActionId, item.Sequence);
									PendingActions.pop_front();
								}

								if (!PendingActions.empty())
									PendingActions.pop_front();

								if (runtimeConfig.UseHighLatencyMitigationLogging)
									Impl.Logger->Format(
										LogCategory::NetworkTimingHandler,
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
								PendingActions.front().CastTimeUs = actorCast.CastTimeUs();

							if (runtimeConfig.UseHighLatencyMitigationLogging)
								Impl.Logger->Format(
									LogCategory::NetworkTimingHandler,
									"{:x}: S2C_ActorCast: actionId={:04x} time={:.3f} target={:08x}",
									conn.Socket(),
									actorCast.ActionId,
									actorCast.CastTimeF,
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
			const auto mode = runtimeConfig.HighLatencyMitigationMode.Value();
			description << std::format(" mode={}", static_cast<int>(mode) + 1);

			// Obtain actual connection latency statistics.
			// Preference for socket latency measurement if available.
			const auto pingTrackerUs = Conn.GetPingLatencyTrackerUs();
			const auto socketLatencyUs = Conn.FetchSocketLatencyUs().value_or(INT64_MAX);
			const auto pingLatencyUs = pingTrackerUs ? pingTrackerUs->Latest() : INT64_MAX;
			auto latencyUs = socketLatencyUs != INT64_MAX ? socketLatencyUs : pingLatencyUs;

			// Additionally, obtain estimated latency for use as fallback.
			const auto rttMinUs = Conn.ApplicationLatencyUs.Min();
			const auto [rttMeanUs, rttDeviationUs] = Conn.ApplicationLatencyUs.MeanAndDeviation();
			const auto latencyEstimateUs = ((rttMinUs + rttMeanUs) / 2) - ((rttDeviationUs + 25000) / 2);

			// Replace latency with estimated latency under certain circumstances:
			// - Failed to obtain measurement
			// - Server RTT measurement is faster than actual latency
			if (latencyUs == INT64_MAX || rttUs < latencyUs) {
				latencyUs = latencyEstimateUs;
				description << std::format(" latency={}us*", latencyUs);
			} else {
				description << std::format(" latency={}us", latencyUs);
			}

			auto delay = 0LL;

			switch (mode) {
				case HighLatencyMitigationMode::SubtractLatency:
					delay = (rttUs - latencyUs);
					break;
				
				case HighLatencyMitigationMode::SimulateRtt:
					delay = Config->Runtime.ExpectedAnimationLockDurationUs.Value();
					break;

				case HighLatencyMitigationMode::SimulateNormalizedRttAndLatency: {
					// Server-side focused mode. Attempts to guess the server delay from response time statistics.
					// Handles fake-ping VPN usage by using estimated latency when necessary.
					auto bestLatencyUs = std::max(latencyUs, latencyEstimateUs);
					
					if(bestLatencyUs != latencyUs) {
						description << std::format("->{}us", bestLatencyUs);
					}

					// Estimate server delay, using modulus to handle high ping rtt multipliers.
					delay = bestLatencyUs > 0 ? ((rttUs % bestLatencyUs) + (rttUs - bestLatencyUs)) / 2 : rttUs;
					break;
				}

				case HighLatencyMitigationMode::StandardGcdDivision: {
					// Client-side focused mode. Emphasizes enforced time slices for animation locks.
					// Advantage is consistent gameplay, however may be disadvantageous in situations where oGCD -> GCD (from downtime for example) may be needed.

					// Calculate new animation lock values based on equal slices of a 2.5 GCD.
					// Assume GCD has 600ms lock time, and remove it from the total GCD time (this will be the weave window).
					const auto gcdUs = 2500000;
					const auto gcdWaitUs = 600000;
					const auto gcdWeaveUs = (gcdUs - gcdWaitUs) - ((gcdUs % gcdWaitUs) / (static_cast<int>(std::floor(gcdUs / gcdWaitUs))));

					// Determine how many weaves we can do given the lock time.
					const auto split = static_cast<int>(std::floor(gcdWeaveUs / originalWaitUs));
					description << std::format(" gcdsplit={}", split);

					// Determine best animation lock value.
					// Calculate the delay value to add on the original lock time.
					// Fallback to latency subtraction if multiple weaving is not possible.
					if(split > 1) {
						delay = (gcdWeaveUs % originalWaitUs) / split;
					} else {
						delay = latencyUs > 0 ? ((rttUs % latencyUs) + (rttUs - latencyUs)) / 2 : rttUs;
					}

					break;
				}
			}

			// Disallow negative delay values.
			delay = std::max(delay, 0LL);

			// Return the new animation lock time without server response time delay, but with artificial delay (safety/lag) value.
			description << std::format(" delay={}us", delay);
			return nowUs + (originalWaitUs - rttUs) + delay;
		}
	};

	NetworkTimingHandler& This;
	Apps::MainApp::App& App;
	const std::shared_ptr<Misc::Logger> Logger;
	SocketHook& SocketHook;
	std::map<SingleConnection*, std::unique_ptr<SingleConnectionHandler>> Handlers{};
	Utils::CallOnDestruction::Multiple Cleanup;

	Implementation(NetworkTimingHandler& this_, Apps::MainApp::App& app)
		: This(this_)
		, App(app)
		, Logger(Misc::Logger::Acquire())
		, SocketHook(app.GetSocketHook()) {
		Cleanup += SocketHook.OnSocketFound([&](SingleConnection& conn) {
			Handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(this, conn));
			});
		Cleanup += SocketHook.OnSocketGone([&](SingleConnection& conn) {
			Handlers.erase(&conn);
			});
	}

	~Implementation() {
		Handlers.clear();
	}

	void CallOnCooldownGroupUpdateListener(uint32_t groupId, bool newDriftItem) const {
		if (const auto it = LastCooldownGroup.find(groupId); it != LastCooldownGroup.end())
			This.OnCooldownGroupUpdateListener(it->second, newDriftItem);
	}

	void CallOnActionRequestListener(const XivIpcs::C2S_ActionRequest& req) const {
		This.OnActionRequestListener(req);
	}
};

XivAlexander::Apps::MainApp::Internal::NetworkTimingHandler::NetworkTimingHandler(Apps::MainApp::App& app)
	: m_pImpl(std::make_unique<Implementation>(*this, app)) {
}

XivAlexander::Apps::MainApp::Internal::NetworkTimingHandler::~NetworkTimingHandler() = default;

const XivAlexander::Apps::MainApp::Internal::NetworkTimingHandler::CooldownGroup& XivAlexander::Apps::MainApp::Internal::NetworkTimingHandler::GetCooldownGroup(uint32_t groupId) const {
	return m_pImpl->LastCooldownGroup[groupId];
}
