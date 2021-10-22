#include "pch.h"
#include "App_ConfigRepository.h"
#include "App_Feature_DamageTracker.h"
#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

struct App::Feature::DamageTracker::Implementation {
	struct Buff {
		uint32_t BuffId;
		uint32_t SourceActorId;
		uint64_t Expiry;
	};

	struct Actor {
		uint32_t Id;
		uint32_t OwnerId;

		std::string Name;
		uint32_t BNpcNameId;

		uint8_t Job;
		uint8_t Level;

		uint32_t AccumulatedIncomingDamage;
		uint32_t AccumulatedIncomingHeal;

		uint32_t AccumulatedDamage;
		uint32_t MaxDamage;
		uint32_t DamageHits;
		uint32_t DamageHitsC;
		uint32_t DamageHitsD;
		uint32_t DamageHitsCD;
		
		uint32_t AccumulatedHeal;
		uint32_t MaxHeal;
		uint32_t HealHits;
		uint32_t HealHitsC;

		uint32_t DamageOverTimeAccumulated;
		uint32_t DamageOverTimeHits;
		uint32_t HealOverTimeAccumulated;
		uint32_t HealOverTimeHits;

		uint32_t DamageTaken;
		uint32_t KillCount;
		uint32_t DeathCount;
		uint32_t Interrupts;

		std::vector<Buff> StatusEffects;
	};

	class SingleConnectionHandler {
	public:
		const std::shared_ptr<Config> m_config;
		Implementation* Impl;
		Network::SingleConnection& Conn;
		std::map<uint32_t, Actor> Actors;

		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: m_config(Config::Acquire())
			, Impl(pImpl)
			, Conn(conn) {
			using namespace Network::Structures;

			const auto& gameConfig = m_config->Game;

			Conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage) {
				if (pMessage->Type != Network::Structures::SegmentType::IPC || pMessage->Data.IPC.Type == IpcType::InterestedType)
					return true;
				
				const auto& ipc = pMessage->Data.IPC;
				if (gameConfig.S2C_ActionEffects[0] == pMessage->Data.IPC.SubType
					|| gameConfig.S2C_ActionEffects[1] == pMessage->Data.IPC.SubType
					|| gameConfig.S2C_ActionEffects[2] == pMessage->Data.IPC.SubType
					|| gameConfig.S2C_ActionEffects[3] == pMessage->Data.IPC.SubType
					|| gameConfig.S2C_ActionEffects[4] == pMessage->Data.IPC.SubType) {

					const auto& actionEffect = pMessage->Data.IPC.Data.S2C_ActionEffect;
					// TODO

				} else if (pMessage->Data.IPC.SubType == gameConfig.S2C_ActorControl) {
					const auto& actorControl = pMessage->Data.IPC.Data.S2C_ActorControl;

					if (actorControl.Category == S2C_ActorControlCategory::CancelCast) {
						auto& actor = Actors[pMessage->SourceActor];
						actor.Id = pMessage->SourceActor;
						actor.Interrupts += 1;

					} else if (actorControl.Category == S2C_ActorControlCategory::Death) {
						const auto& info = actorControl.Death;

						auto& actor = Actors[pMessage->SourceActor];
						actor.Id = pMessage->SourceActor;
						actor.DeathCount += 1;
							
						auto& sourceActor = Actors[info.SourceActorId];
						sourceActor.Id = info.SourceActorId;
						sourceActor.KillCount += 1;

					} else if (actorControl.Category == S2C_ActorControlCategory::EffectOverTime) {
						const auto& info = actorControl.EffectOverTime;

						auto& actor = Actors[pMessage->SourceActor];
						actor.Id = pMessage->SourceActor;
						if (info.EffectType == Network::Structures::IPCMessageDataType::S2C_ActorControl::EffectOverTimeType::EffectTypeEnum::Damage) {
							actor.AccumulatedIncomingDamage += info.Amount;
							// TODO: distribute to sources

						} else if (info.EffectType == Network::Structures::IPCMessageDataType::S2C_ActorControl::EffectOverTimeType::EffectTypeEnum::Heal) {
							actor.AccumulatedIncomingHeal += info.Amount;
							// TODO: distribute to sources
						}
					}
				} else if (pMessage->Data.IPC.SubType == gameConfig.S2C_AddStatusEffect) {
					const auto& info = pMessage->Data.IPC.Data.S2C_AddStatusEffect;

					auto& actor = Actors[pMessage->SourceActor];
					for (int i = 0; i < info.EffectCount; ++i) {
						const auto& effect = info.Effects[i];
						if (effect.EffectIndex >= actor.StatusEffects.size())
							actor.StatusEffects.resize(static_cast<size_t>(1) + effect.EffectIndex);
						
						auto& effectStorage = actor.StatusEffects[effect.EffectIndex];
						effectStorage.BuffId = effect.EffectId;
						effectStorage.Expiry = GetTickCount64() + effect.Duration;
						effectStorage.SourceActorId = effect.SourceActorId;
					}
				}

				switch (ipc.SubType) {
				case 0x00b0: {
					auto& actor = Actors[pMessage->SourceActor];
					actor.Id = pMessage->SourceActor;
					Impl->m_logger->Format(LogCategory::DamageTracker, "{}({:8x}): ActorControl({:04x}:{:04x}): {:x}, {:x}, {:x}, {:x}, {:x}, {:x}",
						actor.Name, actor.Id,
						ipc.SubType, static_cast<uint16_t>(ipc.Data.S2C_ActorControl.Category),
						ipc.Data.S2C_ActorControl.Raw.Padding1,
						ipc.Data.S2C_ActorControl.Raw.Param1,
						ipc.Data.S2C_ActorControl.Raw.Param2,
						ipc.Data.S2C_ActorControl.Raw.Param3,
						ipc.Data.S2C_ActorControl.Raw.Param4,
						ipc.Data.S2C_ActorControl.Raw.Padding2
					);
					break;
				}
				case 0x02b6:  // ActorControlSelf
				case 0x03c5: {  // ActorControlTarget
					auto& actor = Actors[pMessage->SourceActor];
					actor.Id = pMessage->SourceActor;
					Impl->m_logger->Format(LogCategory::DamageTracker, "{}({:8x}): ActorControl({:04x}:{:04x}): {:x}, {:x}, {:x}, {:x}, {:x}, {:x}, {:x}, {:x}",
						actor.Name, actor.Id,
						ipc.SubType, static_cast<uint16_t>(ipc.Data.S2C_ActorControlSelf.Category),
						ipc.Data.S2C_ActorControlSelf.Raw.Padding1,
						ipc.Data.S2C_ActorControlSelf.Raw.Param1,
						ipc.Data.S2C_ActorControlSelf.Raw.Param2,
						ipc.Data.S2C_ActorControlSelf.Raw.Param3,
						ipc.Data.S2C_ActorControlSelf.Raw.Param4,
						ipc.Data.S2C_ActorControlSelf.Raw.Param5,
						ipc.Data.S2C_ActorControlSelf.Raw.Param6,
						ipc.Data.S2C_ActorControlSelf.Raw.Padding2
					);
					break;
				}
				case 0x1a7: {  // Update HP/MP/TP
					auto& actor = Actors[pMessage->SourceActor];
					actor.Id = pMessage->SourceActor;
					break;
				}
				case 0x01d5: {  // Initialize current player
					auto name = std::string(reinterpret_cast<const char*>(&ipc.Data.Raw[0x025f]));
					name.erase(name.begin() + strlen(&name[0]), name.end());
					auto& actor = Actors[pMessage->SourceActor];
					actor.Name = std::move(name);
					break;
				}
				case 0x01d8: // player spawn
				case 0x00d2: // pet spawn
				{
					auto name = std::string(reinterpret_cast<const char*>(&ipc.Data.Raw[0x0230]));
					name.erase(name.begin() + strlen(&name[0]), name.end());
					auto& actor = Actors[pMessage->SourceActor];
					actor.Name = std::move(name);
					actor.OwnerId = *reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0054]);
					actor.BNpcNameId = *reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0044]);
					break;
				}
				/*case 0x0295: {  // Player stat change
					Impl->m_logger->Format(LogCategory::DamageTracker, "{}({:8x}): StatChange: STR={} DEX={} VIT={} INT={} MND={} Crit={} DH={}\n\t{}",
						NameMap.contains(pMessage->SourceActor) ? NameMap.at(pMessage->SourceActor) : "", pMessage->SourceActor,
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0000]),
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0004]),
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0008]),
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x000C]),
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0010]),
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x0048]),
						*reinterpret_cast<const uint32_t*>(&ipc.Data.Raw[0x003C]),
						pMessage->DebugPrint({ .Dump = true, .DumpString = true })
					);
					break;
				}*/
				/*case 0x0320: {  // Player zone change
					Impl->m_logger->Format(LogCategory::DamageTracker, "{}({:8x}): ZoneChange: Zone={}\n\t{}",
						NameMap.contains(pMessage->SourceActor) ? NameMap.at(pMessage->SourceActor) : "", pMessage->SourceActor,
						*reinterpret_cast<const uint16_t*>(&ipc.Data.Raw[0x0002]),
						pMessage->DebugPrint({ .Dump = true, .DumpString = true })
					);
					break;
				}*/
				case 0x03a2: {
					struct GlamAndJobChange {
						uint32_t MainHand;
						uint32_t Unknown_0x004;
						uint32_t OffHand;
						uint32_t Unknown_0x00C;
						uint8_t Unknown_0x010;
						uint8_t Job;
						uint8_t Level;
						uint8_t Unknown_0x013;
						uint32_t Head;
						uint32_t Body;
						uint32_t Hands;
						uint32_t Legs;
						uint32_t Foot;
						uint32_t Earrings;
						uint32_t Neck;
						uint32_t Braces;
						uint32_t RightRing;
						uint32_t LeftRing;
						uint32_t Unknown_0x03C;
					};
					static_assert(sizeof GlamAndJobChange == 0x40);

					const auto& info = *reinterpret_cast<const GlamAndJobChange*>(&ipc.Data.Raw[0]);
					auto& actor = Actors[pMessage->SourceActor];
					actor.Job = info.Job;
					actor.Level = info.Level;
					break;
				}
				}
				return true;
			});
			Conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type != Network::Structures::SegmentType::IPC)
					return true;
				return true;
			});
		}

		~SingleConnectionHandler() {
			Conn.RemoveMessageHandlers(this);
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

App::Feature::DamageTracker::DamageTracker(Network::SocketHook* socketHook)
	: m_pImpl(std::make_unique<Implementation>(socketHook)) {
}

App::Feature::DamageTracker::~DamageTracker() = default;
