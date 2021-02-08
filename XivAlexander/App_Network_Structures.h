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

		static const std::vector<uint8_t> MagicConstant1;
		static const std::vector<uint8_t> MagicConstant2;

		void DebugPrint(const char* head) const;
	};

	constexpr size_t GamePacketHeaderSize = offsetof(FFXIVBundle, Data);

	enum class SegmentType : uint16_t {
		IPC = 3,
		ClientKeepAlive = 7,
		ServerKeepAlive = 8,
	};

	enum class ActionEffectDisplayType : uint8_t {
		HideActionName = 0,
		ShowActionName = 1,
		ShowItemName = 2,
		MountName = 0x0d,
	};

	struct FFXIVMessage {
		uint32_t Length;
		uint32_t SourceActor;
		uint32_t CurrentActor;
		SegmentType Type;
		uint16_t Unknown1;

		union {
			uint8_t Raw[1];
			struct {
				uint32_t Id;
				uint32_t Epoch;
			} KeepAlive;
			struct {
				uint16_t Type;
				uint16_t SubType;
				uint16_t Unknown1;
				uint16_t ServerId;
				uint32_t Epoch;
				uint32_t Unknown2;
				union {
					// See: https://github.com/ravahn/machina/blob/NetworkStructs/Machina.FFXIV/Headers/Global/Server_ActionEffect.cs
					uint8_t Raw[1];
					struct {
						uint32_t AnimationTargetActor;
						uint32_t Unknown1;
						uint32_t ActionId;
						uint32_t GlobalEffectCounter;
						float AnimationLockDuration;
						uint32_t UnknownTargetId;
						uint16_t HideAnimation;
						uint16_t Rotation;
						uint16_t ActionAnimationId;
						uint8_t Variation; // animation
						ActionEffectDisplayType EffectDisplayType;
						uint8_t Unknown2;
						uint8_t EffectCount;
						uint16_t Padding1;
					} ActionEffect;
					struct {
						// 0f 00 00 00  1a 02 00 00  01 00 00 00  9d 40 00 00  00 00 00 00  00 00 00 00
						uint16_t Category;
						uint16_t Padding1;
						uint32_t Param1;
						uint32_t Param2;
						uint32_t Param3;
						uint32_t Param4;
						uint32_t Padding2;
					} ActorControl;
					struct {
						uint16_t Category;
						uint16_t Padding1;
						uint32_t Param1;
						uint32_t Param2;
						uint32_t Param3;
						uint32_t Param4;
						uint32_t Param5;
						uint32_t Param6;
						uint32_t Padding2;
					} ActorControlSelf;
					struct {
						uint16_t ActionId;
						uint8_t SkillType;
						uint8_t Unknown1;
						uint16_t ActionId2;
						uint16_t Unknown2;
						float CastTime;
						uint32_t TargetId;
						float Rotation; // rad
						uint32_t Unknown3;
						uint16_t X;
						uint16_t Y;
						uint16_t Z;
						uint16_t Unknown4;
					} ActorCast;
					struct {
						uint32_t RelatedActionSequence;
						uint32_t ActorId;
						uint32_t CurrentHp;
						uint32_t MaxHp;
						uint16_t CurrentMp;
						uint16_t Unknown1;
						uint16_t MaxMp;
						uint16_t Unknown2;
						uint8_t DamageShield;
						uint8_t EffectCount;
						uint16_t Unknown3;
						struct {
							uint8_t EffectIndex;
							uint8_t Unknown1;
							uint16_t EffectId;
							uint32_t Unknown2;
							float Duration;
							uint32_t SourceActorId;
						} Effects[4];
						uint32_t Unknown4;
					} AddStatusEffect;
					struct {
						uint8_t JobId;
						uint8_t Level1;
						uint8_t Level2;
						uint8_t Level3;
						uint32_t CurrentHp;
						uint32_t MaxHp;
						uint16_t CurrentMp;
						uint16_t MaxMp;
						uint16_t Unknown1;
						uint8_t DamageShield;
						uint8_t Unknown2;
						struct {
							uint16_t EffectId;
							uint16_t OtherInfo;
							float Duration;
							uint32_t ActorId;
						} Effects[30];
					} StatusEffectList;
				} Data;
			} IPC;
		} Data;

		void DebugPrint(const char* head, bool dump = false) const;
	};
}