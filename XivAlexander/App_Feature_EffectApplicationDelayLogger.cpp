#include "pch.h"
#include "App_Feature_EffectApplicationDelayLogger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::EffectApplicationDelayLogger::Internals {
public:

	class SingleConnectionHandler {
	public:
		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace App::Network::Structures;

			const auto& config = ConfigRepository::Config();

			conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>& additionalMessages) {
				const auto now = Utils::GetHighPerformanceCounter();
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (config.S2C_ActionEffects[0] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[1] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[2] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[3] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[4] == pMessage->Data.IPC.SubType) {

						const auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;
						Misc::Logger::GetLogger().Format(
							LogCategory::EffectApplicationDelayLogger,
							"%p: S2C_ActionEffect(%04x): actionId=%04x sourceSequence=%04x wait=%llums",
							conn.GetSocket(),
							pMessage->Data.IPC.SubType,
							actionEffect.ActionId,
							actionEffect.SourceSequence,
							actionEffect.AnimationLockDuration);

					}
					else if (pMessage->Data.IPC.SubType == config.S2C_AddStatusEffect) {
						const auto& addStatusEffect = pMessage->Data.IPC.Data.S2C_AddStatusEffect;
						std::string effects;
						for (int i = 0; i < addStatusEffect.EffectCount; ++i) {
							const auto& entry = addStatusEffect.Effects[i];
							effects += Utils::FormatString(
								"\n\teffectId=%04x duration=%f sourceActorId=%08x",
								entry.EffectId,
								entry.Duration,
								entry.SourceActorId
							);
						}
						Misc::Logger::GetLogger().Format(
							LogCategory::EffectApplicationDelayLogger,
							"%p: S2C_AddStatusEffect: relatedActionSequence=%08x actorId=%08x HP=%d/%d MP=%d shield=%d%s",
							conn.GetSocket(),
							addStatusEffect.RelatedActionSequence,
							addStatusEffect.ActorId,
							addStatusEffect.CurrentHp,
							addStatusEffect.MaxHp,
							addStatusEffect.CurentMp,
							addStatusEffect.DamageShield,
							effects.c_str()
						);
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

App::Feature::EffectApplicationDelayLogger::EffectApplicationDelayLogger()
	: impl(std::make_unique<Internals>()) {
}

App::Feature::EffectApplicationDelayLogger::~EffectApplicationDelayLogger() {
}
