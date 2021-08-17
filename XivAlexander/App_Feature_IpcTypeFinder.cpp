#include "pch.h"
#include "App_Feature_IpcTypeFinder.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::IpcTypeFinder::Implementation {
public:

	class SingleConnectionHandler {
	public:
		Implementation* m_pImpl;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: m_pImpl(pImpl)
			, conn(conn) {
			using namespace Network::Structures;

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->CurrentActor == pMessage->SourceActor) {
						if (pMessage->Length == 0x9c ||
							pMessage->Length == 0x29c ||
							pMessage->Length == 0x4dc ||
							pMessage->Length == 0x71c ||
							pMessage->Length == 0x95c) {
							// Test ActionEffect

							int expectedCount = 0;
							if (pMessage->Length == 0x9c)
								expectedCount = 1;
							else if (pMessage->Length == 0x29c)
								expectedCount = 8;
							else if (pMessage->Length == 0x4dc)
								expectedCount = 16;
							else if (pMessage->Length == 0x71c)
								expectedCount = 24;
							else if (pMessage->Length == 0x95c)
								expectedCount = 32;

							const auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;

							m_pImpl->m_logger->Format(
								LogCategory::IpcTypeFinder,
								"{:x}: S2C_ActionEffect{:02}(0x{:04x}) length={:x} actionId={:04x} sequence={:04x} wait={:.3f}",
								conn.GetSocket(),
								expectedCount,
								pMessage->Data.IPC.SubType,
								pMessage->Length,
								actionEffect.ActionId,
								actionEffect.SourceSequence,
								actionEffect.AnimationLockDuration);
							pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);

						} else if (pMessage->Length == 0x40) {
							// Two possibilities: ActorControlSelf and ActorCast

							//
							// Test ActorControlSelf
							// 
							const auto& actorControlSelf = pMessage->Data.IPC.Data.S2C_ActorControlSelf;
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								const auto& cooldown = actorControlSelf.Cooldown;
								m_pImpl->m_logger->Format(
									LogCategory::IpcTypeFinder,
									"{:x}: S2C_ActorControlSelf(0x{:04x}): Cooldown: actionId={:04x} duration={}",
									conn.GetSocket(),
									pMessage->Data.IPC.SubType,
									cooldown.ActionId,
									cooldown.Duration);
								pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);

							} else if (pMessage->Data.IPC.Data.S2C_ActorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;
								m_pImpl->m_logger->Format(
									LogCategory::IpcTypeFinder,
									"{:x}: S2C_ActorControlSelf(0x{:04x}): Rollback: actionId={:04x} sourceSequence={:04x}",
									conn.GetSocket(),
									pMessage->Data.IPC.SubType,
									rollback.ActionId,
									rollback.SourceSequence);
								pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);
							}

							//
							// Test ActorCast
							//
							m_pImpl->m_logger->Format(
								LogCategory::IpcTypeFinder,
								"{:x}: S2C_ActorCast(0x{:04x}): actionId={:04x} time={:.3f} target={:08x}",
								conn.GetSocket(),
								pMessage->Data.IPC.SubType,
								pMessage->Data.IPC.Data.S2C_ActorCast.ActionId,
								pMessage->Data.IPC.Data.S2C_ActorCast.CastTime,
								pMessage->Data.IPC.Data.S2C_ActorCast.TargetId);
							pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);

						} else if (pMessage->Length == 0x38) {
							// Test ActorControl
							const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;
								m_pImpl->m_logger->Format(
									LogCategory::IpcTypeFinder,
									"{:x}: S2C_ActorControl(0x{:04x}): CancelCast: actionId={:04x}",
									conn.GetSocket(),
									pMessage->Data.IPC.SubType,
									cancelCast.ActionId);
								pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);
							}
						}
					}
					if (pMessage->Length == 0x78) {
						// Test AddStatusEffect
						const auto& actorControl = pMessage->Data.IPC.Data.S2C_AddStatusEffect;
						const auto& addStatusEffect = pMessage->Data.IPC.Data.S2C_AddStatusEffect;
						std::string effects;
						for (int i = 0; i < addStatusEffect.EffectCount; ++i) {
							const auto& entry = addStatusEffect.Effects[i];
							effects += std::format(
								"\n\teffectId={:04x} duration={:g} sourceActorId={:08x}",
								entry.EffectId,
								entry.Duration,
								entry.SourceActorId
							);
						}
						m_pImpl->m_logger->Format(
							LogCategory::IpcTypeFinder,
							"{:x}: S2C_AddStatusEffect(0x{:04x}): relatedActionSequence={:08x} actorId={:08x} HP={}/{} MP={} shield={}{}",
							conn.GetSocket(),
							pMessage->Data.IPC.SubType,
							addStatusEffect.RelatedActionSequence,
							addStatusEffect.ActorId,
							addStatusEffect.CurrentHp,
							addStatusEffect.MaxHp,
							addStatusEffect.CurentMp,
							addStatusEffect.DamageShield,
							effects
						);
					}
				}
				return true;
			});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (pMessage->Length == 0x40) {
						// Test ActionRequest
						const auto& actionRequest = pMessage->Data.IPC.Data.C2S_ActionRequest;
						m_pImpl->m_logger->Format(
							LogCategory::IpcTypeFinder,
							"{:x}: C2S_ActionRequest/GroundTargeted(0x{:04x}): actionId={:04x} sequence={:04x}",
							conn.GetSocket(),
							pMessage->Data.IPC.SubType,
							actionRequest.ActionId, actionRequest.Sequence);
						pMessage->DebugPrint(LogCategory::IpcTypeFinder, "IpcTypeFinder", true);
					}
				}
				return true;
			});
		}
		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
		}
	};

	const std::shared_ptr<Misc::Logger> m_logger;
	Network::SocketHook* const m_socketHook;
	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;
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

App::Feature::IpcTypeFinder::IpcTypeFinder(Network::SocketHook* socketHook)
	: m_pImpl(std::make_unique<Implementation>(socketHook)) {
}

App::Feature::IpcTypeFinder::~IpcTypeFinder() = default;
