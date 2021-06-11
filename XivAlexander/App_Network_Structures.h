#pragma once

namespace App::Network::Structures {

	struct FFXIVBundle {
		uint8_t Magic[16];
		uint64_t Timestamp;		// 16 ~ 23
		uint16_t TotalLength;	// 24 ~ 25
		uint16_t Unknown1;		// 26 ~ 27
		uint16_t ConnType;		// 28 ~ 29
		uint16_t MessageCount;	// 30 ~ 31
		uint8_t Encoding;		// 32
		uint8_t GzipCompressed;	// 33
		uint16_t Unknown2;		// 34 ~ 35
		uint32_t Unknown3;		// 36 ~ 39
		uint8_t Data[1];

		static const uint8_t MagicConstant1[sizeof Magic];
		static const uint8_t MagicConstant2[sizeof Magic];
		[[nodiscard]] static size_t FindPossibleBundleIndex(const void* buf, size_t len);

		void DebugPrint(LogCategory logCategory, const char* head) const;

		[[nodiscard]] static std::vector<std::vector<uint8_t>> SplitMessages(uint16_t expectedMessageCount, const std::span<const uint8_t>& buf);
		[[nodiscard]] std::vector<std::vector<uint8_t>> GetMessages() const;
	};

	constexpr size_t GamePacketHeaderSize = offsetof(FFXIVBundle, Data);

	enum class SegmentType : uint16_t {
		IPC = 3,
		ClientKeepAlive = 7,
		ServerKeepAlive = 8,
	};
	
	enum class IpcType : uint16_t {
		InterestedType = 0x0014,
		CustomType = 0x0e852,
	};

	enum class IpcCustomSubtype : uint16_t {
		OriginalWaitTime = 0x0000,
	};

	enum class ActionEffectDisplayType : uint8_t {
		HideActionName = 0,
		ShowActionName = 1,
		ShowItemName = 2,
		MountName = 0x0d,
	};

	enum class S2C_ActorControlSelfCategory : uint16_t {
		Cooldown = 0x0011,
		ActionRejected = 0x02bc,
	};

	enum class S2C_ActorControlCategory : uint16_t {
		CancelCast = 0x000f,
	};

	struct KeepAliveMessageData {
		uint32_t Id;
		uint32_t Epoch;
	};

	namespace IPCMessageDataType {
		// See: https://github.com/ravahn/machina/blob/NetworkStructs/Machina.FFXIV/Headers/Global/Server_ActionEffect.cs
		// See: https://github.com/SapphireServer/Sapphire/blob/develop/src/common/Network/PacketDef/Zone/ServerZoneDef.h#L557-L563

		struct S2C_AddStatusEffect {
			uint32_t RelatedActionSequence;
			uint32_t ActorId;
			uint32_t CurrentHp;
			uint32_t MaxHp;
			uint16_t CurentMp;
			uint16_t Unknown1;
			uint8_t DamageShield;
			uint8_t EffectCount;
			uint16_t Unknown2;
			
			struct Effect {
				uint8_t EffectIndex;
				uint8_t Unknown1;
				uint16_t EffectId;
				uint16_t Unknown2;
				uint16_t Unknown3;
				float Duration;
				uint32_t SourceActorId;
			} Effects[4];
		};
		struct S2C_ActionEffect {
			uint32_t AnimationTargetActor;
			uint32_t Unknown1;
			uint32_t ActionId;
			uint32_t GlobalEffectCounter;
			float AnimationLockDuration;
			uint32_t UnknownTargetId;
			uint16_t SourceSequence;
			uint16_t Rotation;
			uint16_t ActionAnimationId;
			uint8_t Variation; // animation
			ActionEffectDisplayType EffectDisplayType;
			uint8_t Unknown2;
			uint8_t EffectCount;
			uint16_t Padding1;
		};
		struct S2C_ActorControl {
			union {
				S2C_ActorControlCategory Category;
				struct RawType {
					S2C_ActorControlCategory Category;
					uint16_t Padding1;
					uint32_t Param1;
					uint32_t Param2;
					uint32_t Param3;
					uint32_t Param4;
					uint32_t Padding2;
				} Raw;
				struct CancelCastType {
					S2C_ActorControlCategory Category;
					uint16_t Padding1;
					uint32_t Param1;
					uint32_t Param2;
					uint32_t ActionId;
					uint32_t Param4;
					uint32_t Padding2;
				} CancelCast;
			};
		};
		struct S2C_ActorControlSelf {
			union {
				S2C_ActorControlSelfCategory Category;
				struct RawType {
					S2C_ActorControlSelfCategory Category;
					uint16_t Padding1;
					uint32_t Param1;
					uint32_t Param2;
					uint32_t Param3;
					uint32_t Param4;
					uint32_t Param5;
					uint32_t Param6;
					uint32_t Padding2;
				} Raw;
				struct RollbackType {
					S2C_ActorControlSelfCategory Category;
					uint16_t Padding1;
					uint32_t Param1;
					uint32_t Param2;
					uint32_t ActionId;
					uint32_t Param4;
					uint32_t Param5;
					uint32_t SourceSequence;
					uint32_t Padding2;
				} Rollback;
				struct CooldownType {
					S2C_ActorControlSelfCategory Category;
					uint16_t Padding1;
					uint32_t Param1;
					uint32_t ActionId;
					uint32_t Duration;  // in 10 milliseconds unit
					uint32_t Param4;
					uint32_t Param5;
					uint32_t Param6;
					uint32_t Padding2;
				} Cooldown;
			};
		};
		struct S2C_ActorCast {
			uint16_t ActionId;
			uint8_t SkillType;
			uint8_t Unknown1;
			uint16_t ActionId2;
			uint16_t Unknown2;
			float CastTime;
			uint32_t TargetId;
			float Rotation; // rad
			uint16_t Flag;  // 1 = interruptible blinking
			uint16_t Unknown3;
			uint16_t X;
			uint16_t Y;
			uint16_t Z;
			uint16_t Unknown4;
		};
		struct C2S_ActionRequest {
			uint8_t Pad_0000;
			uint8_t Type;
			uint8_t Pad_0002[2];
			uint32_t ActionId;
			uint16_t Sequence;
			uint8_t Pad_000C[6];
			uint64_t TargetId;
			uint16_t ItemSourceSlot;
			uint16_t ItemSourceContainer;
			uint32_t Unknown;
		};

		struct S2C_Custom_OriginalWaitTime {
			uint16_t SourceSequence;
			uint8_t Pad_0002[2];
			float OriginalWaitTime;
		};
	};
	
	struct IPCMessageData {
		IpcType Type;
		uint16_t SubType;
		uint16_t Unknown1;
		uint16_t ServerId;
		uint32_t Epoch;
		uint32_t Unknown2;
		union {
			uint8_t Raw[1];
			IPCMessageDataType::S2C_ActionEffect S2C_ActionEffect;
			IPCMessageDataType::S2C_ActorControl S2C_ActorControl;
			IPCMessageDataType::S2C_AddStatusEffect S2C_AddStatusEffect;
			IPCMessageDataType::S2C_ActorControlSelf S2C_ActorControlSelf;
			IPCMessageDataType::S2C_ActorCast S2C_ActorCast;
			IPCMessageDataType::C2S_ActionRequest C2S_ActionRequest;
			IPCMessageDataType::S2C_Custom_OriginalWaitTime S2C_Custom_OriginalWaitTime;
		} Data;
	};

	struct FFXIVMessage {
		uint32_t Length;
		uint32_t SourceActor;
		uint32_t CurrentActor;
		SegmentType Type;
		uint16_t Unknown1;

		union {
			uint8_t Raw[1];
			KeepAliveMessageData KeepAlive;
			IPCMessageData IPC;
		} Data;

		void DebugPrint(LogCategory logCategory, const char* head, bool dump = false) const;
	};
}