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
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == 0x14) {
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (pMessage->Length == 0x9c ||
							pMessage->Length == 0x29c ||
							pMessage->Length == 0x4dc ||
							pMessage->Length == 0x71c ||
							pMessage->Length == 0x95c) {
							Misc::Logger::GetLogger().Format(
								"[IpcTypeFinder/AttackResponse] subtype=%04x length=%x skill=%04x wait=%.3f",
								pMessage->Data.IPC.SubType,
								pMessage->Length,
								pMessage->Data.IPC.Data.ActionEffect.ActionId,
								pMessage->Data.IPC.Data.ActionEffect.AnimationLockDuration);
							pMessage->DebugPrint("IpcTypeFinder", true);
						} else if (pMessage->Length == 0x40) {
							if (pMessage->Data.IPC.Data.ActorControlSelf.Category == 0x11) {
								Misc::Logger::GetLogger().Format(
									"[IpcTypeFinder/ActorControlSelf] Cooldown: p1=%08x skill=%04x duration=%d",
									pMessage->Data.IPC.Data.ActorControlSelf.Param1,
									pMessage->Data.IPC.Data.ActorControlSelf.Param2,
									pMessage->Data.IPC.Data.ActorControlSelf.Param3);
								pMessage->DebugPrint("IpcTypeFinder", true);
							} else if (pMessage->Data.IPC.Data.ActorControlSelf.Category == 0x2bc) {
								Misc::Logger::GetLogger().Format(
									"[IpcTypeFinder/ActorControlSelf] Rollback: p1=%08x p2=%08x skill=%04x",
									pMessage->Data.IPC.Data.ActorControlSelf.Param1,
									pMessage->Data.IPC.Data.ActorControlSelf.Param2,
									pMessage->Data.IPC.Data.ActorControlSelf.Param3);
								pMessage->DebugPrint("IpcTypeFinder", true);
							}
							Misc::Logger::GetLogger().Format(
								"[IpcTypeFinder/ActorCast] "
								"skill=%04x type=%02x u1=%02x skill2=%04x u2=%08x time=%.3f "
								"target=%08x rotation=%.3f u3=%08x "
								"x=%d y=%d z=%d u=%04x",
								pMessage->Data.IPC.Data.ActorCast.ActionId,
								pMessage->Data.IPC.Data.ActorCast.SkillType,
								pMessage->Data.IPC.Data.ActorCast.Unknown1,
								pMessage->Data.IPC.Data.ActorCast.ActionId2,
								pMessage->Data.IPC.Data.ActorCast.Unknown2,
								pMessage->Data.IPC.Data.ActorCast.CastTime,

								pMessage->Data.IPC.Data.ActorCast.TargetId,
								pMessage->Data.IPC.Data.ActorCast.Rotation,
								pMessage->Data.IPC.Data.ActorCast.Unknown3,
								pMessage->Data.IPC.Data.ActorCast.X,
								pMessage->Data.IPC.Data.ActorCast.Y,
								pMessage->Data.IPC.Data.ActorCast.Z,
								pMessage->Data.IPC.Data.ActorCast.Unknown4);
							pMessage->DebugPrint("IpcTypeFinder", true);
						} else if (pMessage->Length == 0x38) {
							// Cancel Cast
							if (pMessage->Data.IPC.Data.ActorControl.Category == 0x0f) {
								Misc::Logger::GetLogger().Format(
									"[IpcTypeFinder/ActorControl] CancelCast: p1=%08x p2=%08x skill=%04x",
									pMessage->Data.IPC.Data.ActorControl.Param1,
									pMessage->Data.IPC.Data.ActorControl.Param2,
									pMessage->Data.IPC.Data.ActorControl.Param3);
								pMessage->DebugPrint("IpcTypeFinder", true);
							}
						}
					}
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == 0x14) {
					if (pMessage->Length == 0x40) {
						const auto actionId = *reinterpret_cast<const uint32_t*>(pMessage->Data.IPC.Data.Raw + 4);
						Misc::Logger::GetLogger().Format(
							"[IpcTypeFinder/AttackRequest] skill=%04x",
							actionId);
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
