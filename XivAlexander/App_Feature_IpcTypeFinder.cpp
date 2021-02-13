#include "pch.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

static const float ExtraDelay = 0.1f;

class App::Feature::IpcTypeFinder::Internals {
public:

	class SingleConnectionHandler {
	public:
		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace App::Network::Structures;

			conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (pMessage->Length == 0x9c ||
							pMessage->Length == 0x29c ||
							pMessage->Length == 0x4dc ||
							pMessage->Length == 0x71c ||
							pMessage->Length == 0x95c) {
							// Test ActionEffect

							const auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;

							Misc::Logger::GetLogger().Format(
								"[IpcTypeFinder/AttackResponse] subtype=%04x length=%x actionId=%04x sequence=%04x wait=%.3f",
								pMessage->Data.IPC.SubType,
								pMessage->Length,
								actionEffect.ActionId,
								actionEffect.SourceSequence,
								actionEffect.AnimationLockDuration);
							pMessage->DebugPrint("IpcTypeFinder", true);

						} else if (pMessage->Length == 0x40) {
							// Two possibilities: ActorControlSelf and ActorCast

							//
							// Test ActorControlSelf
							// 
							const auto& actorControlSelf = pMessage->Data.IPC.Data.S2C_ActorControlSelf;
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								const auto& cooldown = actorControlSelf.Cooldown;
								Misc::Logger::GetLogger().Format(
									"[IpcTypeFinder/S2C_ActorControlSelf] Cooldown: p1=%08x actionId=%04x duration=%d",
									cooldown.Param1,
									cooldown.ActionId,
									cooldown.Duration);
								pMessage->DebugPrint("IpcTypeFinder", true);

							} else if (pMessage->Data.IPC.Data.S2C_ActorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;
								Misc::Logger::GetLogger().Format(
									"[IpcTypeFinder/S2C_ActorControlSelf] Rollback: actionId=%04x sourceSequence=%04x",
									rollback.ActionId,
									rollback.SourceSequence);
								pMessage->DebugPrint("IpcTypeFinder", true);
							}

							//
							// Test ActorCast
							//
							Misc::Logger::GetLogger().Format(
								"[IpcTypeFinder/S2C_ActorCast] "
								"actionId=%04x type=%02x u1=%02x skill2=%04x u2=%08x time=%.3f "
								"target=%08x rotation=%.3f u3=%08x "
								"x=%d y=%d z=%d u=%04x",
								pMessage->Data.IPC.Data.S2C_ActorCast.ActionId,
								pMessage->Data.IPC.Data.S2C_ActorCast.SkillType,
								pMessage->Data.IPC.Data.S2C_ActorCast.Unknown1,
								pMessage->Data.IPC.Data.S2C_ActorCast.ActionId2,
								pMessage->Data.IPC.Data.S2C_ActorCast.Unknown2,
								pMessage->Data.IPC.Data.S2C_ActorCast.CastTime,

								pMessage->Data.IPC.Data.S2C_ActorCast.TargetId,
								pMessage->Data.IPC.Data.S2C_ActorCast.Rotation,
								pMessage->Data.IPC.Data.S2C_ActorCast.Unknown3,
								pMessage->Data.IPC.Data.S2C_ActorCast.X,
								pMessage->Data.IPC.Data.S2C_ActorCast.Y,
								pMessage->Data.IPC.Data.S2C_ActorCast.Z,
								pMessage->Data.IPC.Data.S2C_ActorCast.Unknown4);
							pMessage->DebugPrint("IpcTypeFinder", true);

						} else if (pMessage->Length == 0x38) {
							// Test ActorControl
							const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;
								Misc::Logger::GetLogger().Format(
									"[IpcTypeFinder/S2C_ActorControl] CancelCast: p1=%08x actionId=%04x p2=%08x p4=%08x pad1=%04x pad2=%08x",
									pMessage->Length,
									cancelCast.Param1,
									cancelCast.ActionId,
									cancelCast.Param3,
									cancelCast.Param4,
									cancelCast.Padding1,
									cancelCast.Padding2);
								pMessage->DebugPrint("IpcTypeFinder", true);
							}
						}
					}
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->Length == 0x40) {
						// Test ActionRequest
						const auto& actionRequest = pMessage->Data.IPC.Data.C2S_ActionRequest;
						Misc::Logger::GetLogger().Format(
							"[IpcTypeFinder/AttackRequest] actionId=%04x sequence=%04x",
							actionRequest.ActionId, actionRequest.Sequence);
						pMessage->DebugPrint("IpcTypeFinder", true);
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

App::Feature::IpcTypeFinder::IpcTypeFinder()
: impl(std::make_unique<Internals>()){
}

App::Feature::IpcTypeFinder::~IpcTypeFinder() {
}
