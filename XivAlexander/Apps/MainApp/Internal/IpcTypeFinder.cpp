#include "pch.h"
#include "Apps/MainApp/Internal/IpcTypeFinder.h"

#include <XivAlexanderCommon/Sqex/Network/Structure.h>

#include "Apps/MainApp/App.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Misc/Logger.h"

using namespace Sqex::Network::Structure;

struct XivAlexander::Apps::MainApp::Internal::IpcTypeFinder::Implementation {
	class SingleConnectionHandler {
	public:
		Implementation& Impl;
		SingleConnection& Conn;

		SingleConnectionHandler(Implementation& pImpl, SingleConnection& conn)
			: Impl(pImpl)
			, Conn(conn) {

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
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

							const auto& actionEffect = pMessage->Data.Ipc.Data.S2C_ActionEffect;

							Impl.Logger->Format(
								LogCategory::IpcTypeFinder,
								"{:x}: S2C_ActionEffect{:02}(0x{:04x}) length={:x} actionId={:04x} sequence={:04x} wait={:.3f}\n{}",
								conn.Socket(),
								expectedCount,
								pMessage->Data.Ipc.SubType,
								pMessage->Length,
								actionEffect.ActionId,
								actionEffect.SourceSequence,
								actionEffect.AnimationLockDuration,
								pMessage->Represent(true));

						} else if (pMessage->Length == sizeof XivMessageHeader + sizeof XivIpcHeader + sizeof XivIpcs::S2C_ActorControlSelf) {
							// Two possibilities: ActorControlSelf and ActorCast
							static_assert(sizeof XivIpcs::S2C_ActorControlSelf == sizeof XivIpcs::S2C_ActorCast);

							//
							// Test ActorControlSelf
							// 
							const auto& actorControlSelf = pMessage->Data.Ipc.Data.S2C_ActorControlSelf;
							if (actorControlSelf.Category == S2C_ActorControlSelfCategory::Cooldown) {
								const auto& cooldown = actorControlSelf.Cooldown;
								Impl.Logger->Format(
									LogCategory::IpcTypeFinder,
									"{:x}: S2C_ActorControlSelf(0x{:04x}): Cooldown: actionId={:04x} duration={}\n{}",
									conn.Socket(),
									pMessage->Data.Ipc.SubType,
									cooldown.ActionId,
									cooldown.Duration,
									pMessage->Represent(true));

							} else if (pMessage->Data.Ipc.Data.S2C_ActorControlSelf.Category == S2C_ActorControlSelfCategory::ActionRejected) {
								const auto& rollback = actorControlSelf.Rollback;
								Impl.Logger->Format(
									LogCategory::IpcTypeFinder,
									"{:x}: S2C_ActorControlSelf(0x{:04x}): Rollback: actionId={:04x} sourceSequence={:04x}\n{}",
									conn.Socket(),
									pMessage->Data.Ipc.SubType,
									rollback.ActionId,
									rollback.SourceSequence,
									pMessage->Represent(true));
							}

							//
							// Test ActorCast
							//
							Impl.Logger->Format(
								LogCategory::IpcTypeFinder,
								"{:x}: S2C_ActorCast(0x{:04x}): actionId={:04x} time={:.3f} target={:08x}\n{}",
								conn.Socket(),
								pMessage->Data.Ipc.SubType,
								pMessage->Data.Ipc.Data.S2C_ActorCast.ActionId,
								pMessage->Data.Ipc.Data.S2C_ActorCast.CastTime,
								pMessage->Data.Ipc.Data.S2C_ActorCast.TargetId,
								pMessage->Represent(true));

						} else if (pMessage->Length == 0x38) {
							// Test ActorControl
							const auto& actorControl = pMessage->Data.Ipc.Data.S2C_ActorControl;
							if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
								const auto& cancelCast = actorControl.CancelCast;
								Impl.Logger->Format(
									LogCategory::IpcTypeFinder,
									"{:x}: S2C_ActorControl(0x{:04x}): CancelCast: actionId={:04x}\n{}",
									conn.Socket(),
									pMessage->Data.Ipc.SubType,
									cancelCast.ActionId,
									pMessage->Represent(true));
							}
						}
					}
					if (pMessage->Length == sizeof XivMessageHeader + sizeof XivIpcHeader + sizeof XivIpcs::S2C_EffectResult5) {
						const auto& effectResult = pMessage->Data.Ipc.Data.S2C_EffectResult5;
						std::string effects;
						for (int i = 0; i < effectResult.EffectCount; ++i) {
							const auto& entry = effectResult.Effects[i];
							effects += std::format(
								"\n\teffectId={:04x} duration={:g} sourceActorId={:08x}",
								entry.EffectId,
								entry.Duration,
								entry.SourceActorId
							);
						}
						Impl.Logger->Format(
							LogCategory::IpcTypeFinder,
							"{:x}: S2C_EffectResult5(0x{:04x}): relatedActionSequence={:08x} actorId={:08x} HP={}/{} MP={} shield={}{}",
							conn.Socket(),
							pMessage->Data.Ipc.SubType,
							effectResult.RelatedActionSequence,
							effectResult.ActorId,
							effectResult.CurrentHp,
							effectResult.MaxHp,
							effectResult.CurentMp,
							effectResult.DamageShield,
							effects
						);
					} else if (pMessage->Length == sizeof XivMessageHeader + sizeof XivIpcHeader + sizeof XivIpcs::S2C_EffectResult6) {
						const auto& effectResult = pMessage->Data.Ipc.Data.S2C_EffectResult6;
						std::string effects;
						for (int i = 0; i < effectResult.EffectCount; ++i) {
							const auto& entry = effectResult.Effects[i];
							effects += std::format(
								"\n\teffectId={:04x} duration={:g} sourceActorId={:08x}",
								entry.EffectId,
								entry.Duration,
								entry.SourceActorId
							);
						}
						Impl.Logger->Format(
							LogCategory::IpcTypeFinder,
							"{:x}: S2C_EffectResult6(0x{:04x}): relatedActionSequence={:08x} actorId={:08x} HP={}/{} MP={} shield={}{}",
							conn.Socket(),
							pMessage->Data.Ipc.SubType,
							effectResult.RelatedActionSequence,
							effectResult.ActorId,
							effectResult.CurrentHp,
							effectResult.MaxHp,
							effectResult.CurentMp,
							effectResult.DamageShield,
							effects
						);
					} else if (pMessage->Length == sizeof XivMessageHeader + sizeof XivIpcHeader + sizeof XivIpcs::S2C_EffectResult6Basic) {
						const auto& effectResult = pMessage->Data.Ipc.Data.S2C_EffectResult6Basic;
						Impl.Logger->Format(
							LogCategory::IpcTypeFinder,
							"{:x}: S2C_EffectResult6Basic(0x{:04x}): relatedActionSequence={:08x} actorId={:08x} HP={}",
							conn.Socket(),
							pMessage->Data.Ipc.SubType,
							effectResult.RelatedActionSequence,
							effectResult.ActorId,
							effectResult.CurrentHp
						);
					}
				}
				return true;
			});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
					if (pMessage->Length == 0x40) {
						// Test ActionRequest
						const auto& actionRequest = pMessage->Data.Ipc.Data.C2S_ActionRequest;
						Impl.Logger->Format(
							LogCategory::IpcTypeFinder,
							"{:x}: C2S_ActionRequest/GroundTargeted(0x{:04x}): actionId={:04x} sequence={:04x}\n{}",
							conn.Socket(),
							pMessage->Data.Ipc.SubType,
							actionRequest.ActionId, actionRequest.Sequence,
							pMessage->Represent(true));
					}
				}
				return true;
			});
		}

		~SingleConnectionHandler() {
			Conn.RemoveMessageHandlers(this);
		}
	};

	const std::shared_ptr<Misc::Logger> Logger;
	std::map<SingleConnection*, std::unique_ptr<SingleConnectionHandler>> Handlers;
	Utils::CallOnDestruction::Multiple Cleanup;

	Implementation(App& app)
		: Logger(Misc::Logger::Acquire()) {
		Cleanup += app.GetSocketHook().OnSocketFound([&](SingleConnection& conn) {
			Handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
			});
		Cleanup += app.GetSocketHook().OnSocketGone([&](SingleConnection& conn) {
			Handlers.erase(&conn);
			});
	}

	~Implementation() {
		Handlers.clear();
	}
};

XivAlexander::Apps::MainApp::Internal::IpcTypeFinder::IpcTypeFinder(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Internal::IpcTypeFinder::~IpcTypeFinder() = default;
