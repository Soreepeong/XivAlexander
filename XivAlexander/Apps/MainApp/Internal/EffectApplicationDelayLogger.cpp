#include "pch.h"
#include "Apps/MainApp/Internal/EffectApplicationDelayLogger.h"

#include <XivAlexanderCommon/Sqex/Network/Structure.h>

#include "Apps/MainApp/App.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Config.h"
#include "Misc/Logger.h"

using namespace Sqex::Network::Structure;

struct XivAlexander::Apps::MainApp::Internal::EffectApplicationDelayLogger::Implementation {
	class SingleConnectionHandler {
		const std::shared_ptr<Config> m_config;

	public:
		Implementation& Impl;
		SingleConnection& Conn;

		SingleConnectionHandler(Implementation& impl, SingleConnection& conn)
			: m_config(Config::Acquire())
			, Impl(impl)
			, Conn(conn) {

			const auto& config = m_config->Game;

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
					if (config.S2C_ActionEffects[0] == pMessage->Data.Ipc.SubType
						|| config.S2C_ActionEffects[1] == pMessage->Data.Ipc.SubType
						|| config.S2C_ActionEffects[2] == pMessage->Data.Ipc.SubType
						|| config.S2C_ActionEffects[3] == pMessage->Data.Ipc.SubType
						|| config.S2C_ActionEffects[4] == pMessage->Data.Ipc.SubType) {

						const auto& actionEffect = pMessage->Data.Ipc.Data.S2C_ActionEffect;
						Impl.Logger->Format(
							LogCategory::EffectApplicationDelayLogger,
							"{:x}: S2C_ActionEffect({:04x}): actionId={:04x} sourceSequence={:04x} wait={}us",
							conn.Socket(),
							pMessage->Data.Ipc.SubType,
							actionEffect.ActionId,
							actionEffect.SourceSequence,
							actionEffect.AnimationLockDurationUs());

					} else if (pMessage->Data.Ipc.SubType == config.S2C_EffectResult5) {
						const auto& effectResult = pMessage->Data.Ipc.Data.S2C_EffectResult5;
						std::string effects;
						for (int i = 0; i < effectResult.EffectCount; ++i) {
							const auto& entry = effectResult.Effects[i];
							effects += std::format(
								"\n\teffectId={:04x} duration={:.3f} sourceActorId={:08x}",
								entry.EffectId,
								entry.DurationF,
								entry.SourceActorId
							);
						}
						Impl.Logger->Format(
							LogCategory::EffectApplicationDelayLogger,
							"{:x}: S2C_EffectResult5: relatedActionSequence={:08x} actorId={:08x} HP={}/{} MP={} shield={}{}",
							conn.Socket(),
							effectResult.RelatedActionSequence,
							effectResult.ActorId,
							effectResult.CurrentHp,
							effectResult.MaxHp,
							effectResult.CurentMp,
							effectResult.DamageShield,
							effects
						);
					} else if (pMessage->Data.Ipc.SubType == config.S2C_EffectResult6) {
						const auto& effectResult = pMessage->Data.Ipc.Data.S2C_EffectResult6;
						std::string effects;
						for (int i = 0; i < effectResult.EffectCount; ++i) {
							const auto& entry = effectResult.Effects[i];
							effects += std::format(
								"\n\teffectId={:04x} duration={:.3f} sourceActorId={:08x}",
								entry.EffectId,
								entry.DurationF,
								entry.SourceActorId
							);
						}
						Impl.Logger->Format(
							LogCategory::EffectApplicationDelayLogger,
							"{:x}: S2C_EffectResult6: relatedActionSequence={:08x} actorId={:08x} HP={}/{} MP={} shield={}{}",
							conn.Socket(),
							effectResult.RelatedActionSequence,
							effectResult.ActorId,
							effectResult.CurrentHp,
							effectResult.MaxHp,
							effectResult.CurentMp,
							effectResult.DamageShield,
							effects
						);
					} else if (pMessage->Data.Ipc.SubType == config.S2C_EffectResult6Basic) {
						const auto& effectResult = pMessage->Data.Ipc.Data.S2C_EffectResult6Basic;
						Impl.Logger->Format(
							LogCategory::EffectApplicationDelayLogger,
							"{:x}: S2C_EffectResult6Basic: relatedActionSequence={:08x} actorId={:08x} HP={}",
							conn.Socket(),
							effectResult.RelatedActionSequence,
							effectResult.ActorId,
							effectResult.CurrentHp
						);
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

XivAlexander::Apps::MainApp::Internal::EffectApplicationDelayLogger::EffectApplicationDelayLogger(App& socketHook)
	: m_pImpl(std::make_unique<Implementation>(socketHook)) {
}

XivAlexander::Apps::MainApp::Internal::EffectApplicationDelayLogger::~EffectApplicationDelayLogger() = default;
