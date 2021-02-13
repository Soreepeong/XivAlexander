#include "pch.h"
#include "App_Feature_AnimationLockLatencyHandler.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::AnimationLockLatencyHandler::Internals {
public:
	// Server responses have been usually taking between 50ms and 100ms on below-1ms
	// latency to server, so 75ms is a good average.
	// The server will do sanity check on the frequency of action use requests,
	// and it's very easy to identify whether you're trying to go below allowed minimum value.
	// This addon is already in gray area. Do NOT decrease this value. You've been warned.
	// Feel free to increase and see how does it feel like to play on high latency instead, though.
	static inline const uint64_t ExtraDelay = 75;  // in milliseconds

	static inline const uint64_t AutoAttackDelay = 100; // in milliseconds

	class SingleConnectionHandler {
		const uint64_t CAST_SENTINEL = 0;

		struct PendingAction {
			const uint16_t ActionId;
			const uint16_t Sequence;
			const uint64_t RequestTimestamp;
			bool CastFlag = false;

			PendingAction(const Network::Structures::IPCMessageDataType::C2S_ActionRequest& request)
				: ActionId(request.ActionId)
				, Sequence(request.Sequence)
				, RequestTimestamp(Utils::GetHighPerformanceCounter()){
			}
		};

	public:
		// The game will allow the user to use an action, if server does not respond in 500ms since last action usage.
		// This will result in cancellation of following actions, so to prevent this, we keep track of outgoing action
		// request timestamps, and stack up required animation lock time responses from server.
		// The game will only process the latest animation lock duration information.
		std::deque<PendingAction> m_pendingActions;
		uint64_t m_lastAnimationLockEndsAt = 0;
		std::map<int, uint64_t> m_originalWaitTimeMap;

		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace App::Network::Structures;

			const auto& config = ConfigRepository::Config();

			conn.AddOutgoingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->Data.IPC.SubType == config.C2S_ActionRequest) {
						const auto& actionRequest = pMessage->Data.IPC.Data.C2S_ActionRequest;
						m_pendingActions.emplace_back(actionRequest);

						// If somehow latest action request has been made before last animation lock end time, keep it.
						// Otherwise...
						if (m_pendingActions.back().RequestTimestamp > m_lastAnimationLockEndsAt) {

							// If there was no action queued to begin with before the current one, update the base lock time to now.
							if (m_pendingActions.size() == 1)
								m_lastAnimationLockEndsAt = m_pendingActions.back().RequestTimestamp;
						}

						Misc::Logger::GetLogger().Format(
							"C2S_ActionRequest: actionId=%04x sequence=%04x",
							actionRequest.ActionId,
							actionRequest.Sequence);
					}
				}
				return true;
				});
			conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>& additionalMessages) {
				const auto now = Utils::GetHighPerformanceCounter();

				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::CustomType) {
					if (pMessage->Data.IPC.SubType == static_cast<uint16_t>(IpcCustomSubtype::OriginalWaitTime)) {
						const auto& data = pMessage->Data.IPC.Data.S2C_Custom_OriginalWaitTime;
						m_originalWaitTimeMap[data.SourceSequence] = static_cast<uint64_t>(static_cast<double>(data.OriginalWaitTime) * 1000ULL);
					}

					// Don't relay custom IPC data to game.
					return false;

				} else if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					// Only interested in messages intended for the current player
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (config.S2C_ActionEffects[0] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[1] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[2] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[3] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[4] == pMessage->Data.IPC.SubType) {

							// actionEffect has to be modified later on, so no const
							auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;
							int64_t originalWaitTime, waitTime;

							{
								const auto it = m_originalWaitTimeMap.find(actionEffect.SourceSequence);
								if (it == m_originalWaitTimeMap.end())
									waitTime = originalWaitTime = static_cast<int64_t>(static_cast<double>(actionEffect.AnimationLockDuration) * 1000ULL);
								else {
									waitTime = originalWaitTime = it->second;
									m_originalWaitTimeMap.erase(it);
								}
							}

							if (actionEffect.SourceSequence == 0) {
								if (actionEffect.ActionId == 0x0007  // auto-attack
									|| actionEffect.ActionId == 0x0008  // MCH auto-attack
									) {
									if (m_lastAnimationLockEndsAt > now) {
										// if animation lock is supposedly already in progress, add the new value to previously in-progress animation lock, instead of replacing it.
										m_lastAnimationLockEndsAt += AutoAttackDelay;
										waitTime = m_lastAnimationLockEndsAt - now;

									} else {
										// even if it wasn't, the server would consider other actions in progress when calculating auto-attack delay, so we fix it to 100ms.
										waitTime = AutoAttackDelay;
									}
								} else
									Misc::Logger::GetLogger().Format(reinterpret_cast<const char*>(u8"\t┎ Not user-originated, and isn't an auto-attack (%04x)"), actionEffect.ActionId);

							} else {
								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() && m_pendingActions.front().Sequence != actionEffect.SourceSequence) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format(reinterpret_cast<const char*>(u8"\t┎ ActionRequest ignored for processing: actionId=%04x sequence=%04x"),
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty()) {
									const auto& item = m_pendingActions.front();
									// 100ms animation lock after cast ends stays. Modify animation lock duration for instant actions only.
									// Since no other action is in progress right before the cast ends, we can safely replace the animation lock with the latest after-cast lock.
									if (!item.CastFlag) {
										m_lastAnimationLockEndsAt += originalWaitTime + ExtraDelay;
										waitTime = m_lastAnimationLockEndsAt - now;
									}
									m_pendingActions.pop_front();
								}
							}
							if (waitTime != originalWaitTime) {
								actionEffect.AnimationLockDuration = std::max(0LL, waitTime) / 1000.f;
								Misc::Logger::GetLogger().Format(
									"S2C_ActionEffect: actionId=%04x sourceSequence=%04x wait=%llums->%llums",
									actionEffect.ActionId,
									actionEffect.SourceSequence,
									originalWaitTime, waitTime);

							} else {
								Misc::Logger::GetLogger().Format(
									"S2C_ActionEffect: actionId=%04x sourceSequence=%04x wait=%llums",
									actionEffect.ActionId,
									actionEffect.SourceSequence,
									originalWaitTime);
							}

						} else if (pMessage->Data.IPC.SubType == config.S2C_ActorControlSelf) {
							const auto& actorControlSelf = pMessage->Data.IPC.Data.S2C_ActorControlSelf;

							// Oldest action request has been rejected from server.
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;

								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() 
									&& (
										// Sometimes SourceSequence is empty, in which case, we use ActionId to judge.
										(rollback.SourceSequence != 0 && m_pendingActions.front().Sequence != rollback.SourceSequence)
										|| (rollback.SourceSequence == 0 && m_pendingActions.front().ActionId != rollback.ActionId)
									)) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format(reinterpret_cast<const char*>(u8"\t┎ ActionRequest ignored for processing: actionId=%04x sequence=%04x"),
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								Misc::Logger::GetLogger().Format(
									"S2C_ActorControlSelf/ActionRejected: actionId=%04x sourceSequence=%08x",
									rollback.ActionId,
									rollback.SourceSequence);
							}

						} else if (pMessage->Data.IPC.SubType == config.S2C_ActorControl) {
							const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;
							
							// The server has cancelled an oldest action (which is a cast) in progress.
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;

								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty()&& m_pendingActions.front().ActionId != cancelCast.ActionId) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format(reinterpret_cast<const char*>(u8"\t┎ ActionRequest ignored for processing: actionId=%04x sequence=%04x"),
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								Misc::Logger::GetLogger().Format(
									"S2C_ActorControl/CancelCast: actionId=%04x",
									cancelCast.ActionId);
							}

						} else if (pMessage->Data.IPC.SubType == config.S2C_ActorCast) {
							const auto& actorCast = pMessage->Data.IPC.Data.S2C_ActorCast;
							// Mark that the last request was a cast.
							// If it indeed is a cast, the game UI will block the user from generating additional requests,
							// so first item is guaranteed to be the cast action.
							if (!m_pendingActions.empty())
								m_pendingActions.front().CastFlag = true;

							Misc::Logger::GetLogger().Format(
								"S2C_ActorCast: actionId=%04x time=%.3f target=%08x",
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
	};

	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;

	Internals() {
		Network::SocketHook::Instance()->AddOnSocketFoundListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
		});
		Network::SocketHook::Instance()->AddOnSocketGoneListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
		});
	}

	~Internals() {
		m_handlers.clear();
		Network::SocketHook::Instance()->RemoveListeners(this);
	}
};

App::Feature::AnimationLockLatencyHandler::AnimationLockLatencyHandler()
: impl(std::make_unique<Internals>()){
}

App::Feature::AnimationLockLatencyHandler::~AnimationLockLatencyHandler() {
}
