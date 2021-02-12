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
	static inline const double ExtraDelay = 0.075;

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

						if (m_pendingActions.size() == 1)
							m_lastAnimationLockEndsAt = m_pendingActions.front().RequestTimestamp;

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

				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					// Only interested in messages intended for the current player
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (config.S2C_ActionEffects[0] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[1] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[2] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[3] == pMessage->Data.IPC.SubType
							|| config.S2C_ActionEffects[4] == pMessage->Data.IPC.SubType) {

							// actionEffect has to be modified later on, so no const
							auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;
							Misc::Logger::GetLogger().Format(
								"S2C_ActionEffect: actionId=%04x sourceSequence=%04x wait=%.3f",
								actionEffect.ActionId,
								actionEffect.SourceSequence,
								actionEffect.AnimationLockDuration);

							if (actionEffect.SourceSequence != 0) {
								// find the one sharing Sequence, assuming action responses are always in order
								while (!m_pendingActions.empty() && m_pendingActions.front().Sequence != actionEffect.SourceSequence) {
									const auto& item = m_pendingActions.front();
									Misc::Logger::GetLogger().Format("\t=> Action ignored for processing: actionId=%04x sequence=%04x",
										item.ActionId, item.Sequence);
									m_pendingActions.pop_front();
								}

								if (!m_pendingActions.empty()) {
									const auto& item = m_pendingActions.front();
									// 100ms animation lock after cast ends stays. Modify animation lock duration for instant actions only.
									if (!item.CastFlag) {
										auto extraDelay = ExtraDelay;
										if (extraDelay <= 0.07) {
											// I told you to not decrease the value below 70ms.
											if (rand() % 10000 < 50) {
												// This is what you get for decreasing the value.
												extraDelay = 5;
											}
										}
										const auto addedDelay = static_cast<uint64_t>(1000. * std::max(0., extraDelay + actionEffect.AnimationLockDuration));
										m_lastAnimationLockEndsAt += addedDelay;
										actionEffect.AnimationLockDuration = std::max(0.f, static_cast<int64_t>(m_lastAnimationLockEndsAt - now) / 1000.f);

										Misc::Logger::GetLogger().Format("\t=> wait time changed to %.3f", actionEffect.AnimationLockDuration);
									}
									m_pendingActions.pop_front();
								}
							}

						} else if (pMessage->Data.IPC.SubType == config.S2C_ActorControlSelf) {
							const auto& actorControlSelf = pMessage->Data.IPC.Data.S2C_ActorControlSelf;

							// Latest action request has been rejected from server.
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								Misc::Logger::GetLogger().Format(
									"S2C_ActorControlSelf/ActionRejected: p1=%08x p2=%08x actionId=%04x p4=%08x p5=%08x p6=%08x",
									rollback.Param1,
									rollback.Param2,
									rollback.ActionId,
									rollback.Param4,
									rollback.Param5,
									rollback.Param6);
							}

						} else if (pMessage->Data.IPC.SubType == config.S2C_ActorControl) {
							const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;
							
							// The server has cancelled a cast in progress.
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;

								if (!m_pendingActions.empty())
									m_pendingActions.pop_front();

								Misc::Logger::GetLogger().Format(
									"S2C_ActorControl/CancelCast: p1=%08x actionId=%04x p2=%08x p4=%08x pad1=%04x pad2=%08x",
									cancelCast.Param1,
									cancelCast.ActionId,
									cancelCast.Param3,
									cancelCast.Param4,
									cancelCast.Padding1,
									cancelCast.Padding2);
							}

						} else if (pMessage->Data.IPC.SubType == config.S2C_ActorCast) {
							const auto& actorCast = pMessage->Data.IPC.Data.S2C_ActorCast;
							// Mark that the last request was a cast.
							// If it indeed is a cast, the game UI will block the user from generating additional requests,
							// so first item is guaranteed to be the cast action.
							if (!m_pendingActions.empty())
								m_pendingActions.front().CastFlag = true;

							Misc::Logger::GetLogger().Format(
								"S2C_ActorCast: "
								"actionId=%04x type=%02x u1=%02x skill2=%04x u2=%08x time=%.3f "
								"target=%08x rotation=%.3f u3=%08x "
								"x=%d y=%d z=%d u=%04x",

								actorCast.ActionId,
								actorCast.SkillType,
								actorCast.Unknown1,
								actorCast.ActionId2,
								actorCast.Unknown2,
								actorCast.CastTime,

								actorCast.TargetId,
								actorCast.Rotation,
								actorCast.Unknown3,
								actorCast.X,
								actorCast.Y,
								actorCast.Z,
								actorCast.Unknown4);
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

void App::Feature::AnimationLockLatencyHandler::ReloadConfig() {
	ConfigRepository::Config().Reload();
}
