#include "pch.h"
#include "App_Feature_EffectApplicationDelayLogger.h"

#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

struct App::Feature::EffectApplicationDelayLogger::Implementation {
	class SingleConnectionHandler {
		const std::shared_ptr<Config> m_config;

	public:
		Implementation* m_pImpl;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: m_config(Config::Acquire())
			, m_pImpl(pImpl)
			, conn(conn) {
			using namespace Network::Structures;

			const auto& config = m_config->Game;

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					if (config.S2C_ActionEffects[0] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[1] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[2] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[3] == pMessage->Data.IPC.SubType
						|| config.S2C_ActionEffects[4] == pMessage->Data.IPC.SubType) {

						const auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;
						m_pImpl->m_logger->Format(
							LogCategory::EffectApplicationDelayLogger,
							"{:x}: S2C_ActionEffect({:04x}): actionId={:04x} sourceSequence={:04x} wait={}ms",
							conn.GetSocket(),
							pMessage->Data.IPC.SubType,
							actionEffect.ActionId,
							actionEffect.SourceSequence,
							static_cast<int>(1000 * actionEffect.AnimationLockDuration));

					} else if (pMessage->Data.IPC.SubType == config.S2C_AddStatusEffect) {
						const auto& addStatusEffect = pMessage->Data.IPC.Data.S2C_AddStatusEffect;
						std::string effects;
						for (int i = 0; i < addStatusEffect.EffectCount; ++i) {
							const auto& entry = addStatusEffect.Effects[i];
							effects += std::format(
								"\n\teffectId={:04x} duration={:.3f} sourceActorId={:08x}",
								entry.EffectId,
								entry.Duration,
								entry.SourceActorId
							);
						}
						m_pImpl->m_logger->Format(
							LogCategory::EffectApplicationDelayLogger,
							"{:x}: S2C_AddStatusEffect: relatedActionSequence={:08x} actorId={:08x} HP={}/{} MP={} shield={}{}",
							conn.GetSocket(),
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
		}
		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
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

App::Feature::EffectApplicationDelayLogger::EffectApplicationDelayLogger(Network::SocketHook* socketHook)
	: m_pImpl(std::make_unique<Implementation>(socketHook)) {
}

App::Feature::EffectApplicationDelayLogger::~EffectApplicationDelayLogger() = default;
