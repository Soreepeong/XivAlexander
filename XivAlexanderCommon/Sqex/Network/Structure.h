#pragma once

namespace Utils {
	class ZlibReusableInflater;
	class Oodler;
}

namespace Sqex::Network::Structure {
	enum class MessageType : uint16_t {
		Ipc = 3,
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
		int32_t Epoch;
	};

	namespace XivIpcs {
		// See: https://github.com/ravahn/machina/blob/NetworkStructs/Machina.FFXIV/Headers/Global/Server_ActionEffect.cs
		// See: https://github.com/SapphireServer/Sapphire/blob/develop/src/common/Network/PacketDef/Zone/ServerZoneDef.h#L557-L563

		struct S2C_ActionEffect {
			uint32_t AnimationTargetActor;
			uint32_t Unknown1;
			uint32_t ActionId;
			uint32_t GlobalEffectCounter;
			float AnimationLockDurationF;
			uint32_t UnknownTargetId;
			uint16_t SourceSequence;
			uint16_t Rotation;
			uint16_t ActionAnimationId;
			uint8_t Variation;  // animation
			ActionEffectDisplayType EffectDisplayType;
			uint8_t Unknown2;
			uint8_t EffectCount;
			uint16_t Padding1;

			int64_t AnimationLockDurationUs() const { return static_cast<int64_t>(static_cast<double>(AnimationLockDurationF) * 1000000.); }
			void AnimationLockDurationUs(int64_t v) { AnimationLockDurationF = static_cast<float>(static_cast<double>(v) / 1000000.); }
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
					uint32_t CooldownGroupId;
					uint32_t ActionId;
					int32_t Duration10ms;  // in 10 milliseconds unit
					uint32_t Param4;
					uint32_t Param5;
					uint32_t Param6;
					uint32_t Padding2;

					void DurationF(double v) { Duration10ms = static_cast<uint32_t>(100. * v); }
					double DurationF() const { return static_cast<double>(Duration10ms) / 100.; }
					void DurationUs(int64_t v) { Duration10ms = static_cast<int32_t>(v / 10000ULL); }
					int64_t DurationUs() const { return Duration10ms * 10000ULL; };
				} Cooldown;
			};
		};

		struct S2C_ActorCast {
			uint16_t ActionId;
			uint8_t SkillType;
			uint8_t Unknown1;
			uint16_t ActionId2;
			uint16_t Unknown2;
			float CastTimeF;
			uint32_t TargetId;
			float Rotation;  // rad
			uint16_t Flag;  // 1 = interruptible blinking
			uint16_t Unknown3;
			uint16_t X;
			uint16_t Y;
			uint16_t Z;
			uint16_t Unknown4;

			void CastTimeUs(int64_t v) { CastTimeF = static_cast<float>(static_cast<double>(v) / 1000000.); }
			int64_t CastTimeUs() const { return static_cast<int64_t>(static_cast<double>(CastTimeF) * 1000000.); };
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

	struct XivIpcHeader {
		IpcType Type;
		uint16_t SubType;
		uint16_t Unknown1;
		uint16_t ServerId;
		int32_t Epoch;
		uint32_t Unknown2;
	};

	struct XivIpc : XivIpcHeader {
		union {
			uint8_t Raw[1];
			XivIpcs::S2C_ActionEffect S2C_ActionEffect;
			XivIpcs::S2C_ActorControl S2C_ActorControl;
			XivIpcs::S2C_ActorControlSelf S2C_ActorControlSelf;
			XivIpcs::S2C_ActorCast S2C_ActorCast;
			XivIpcs::C2S_ActionRequest C2S_ActionRequest;
			XivIpcs::S2C_Custom_OriginalWaitTime S2C_Custom_OriginalWaitTime;
		} Data;
	};

	struct XivMessageHeader {
		uint32_t Length;
		uint32_t SourceActor;
		uint32_t CurrentActor;
		MessageType Type;
		uint16_t Unknown1;
	};

	struct XivMessage : XivMessageHeader {
		union {
			uint8_t Raw[1];
			KeepAliveMessageData KeepAlive;
			XivIpc Ipc;
		} Data;

		std::string Represent(bool dump = false) const;
	};

	enum class CompressionType : uint8_t {
		None = 0,
		Deflate = 1,
		Oodle = 2,
	};

	struct XivBundleHeader {
		uint8_t Magic[16];
		int64_t Timestamp;		// 16 ~ 23
		uint32_t TotalLength;	// 24 ~ 27
		uint16_t ConnType;		// 28 ~ 29
		uint16_t MessageCount;	// 30 ~ 31
		uint8_t Encoding;		// 32
		CompressionType CompressionType;	// 33
		uint16_t Unknown2;		// 34 ~ 35
		uint32_t DecodedBodyLength; // 36 ~ 39
	};

	struct XivBundle : XivBundleHeader {
		uint8_t Data[1];

		static const uint8_t MagicConstant1[sizeof Magic];
		static const uint8_t MagicConstant2[sizeof Magic];
		[[nodiscard]] static std::span<const uint8_t> ExtractFrontTrash(const std::span<const uint8_t>& buf);

		std::string Represent() const;

		[[nodiscard]] static std::vector<std::vector<uint8_t>> SplitMessages(uint16_t expectedMessageCount, const std::span<const uint8_t>& buf);
		[[nodiscard]] std::vector<std::vector<uint8_t>> GetMessages(Utils::ZlibReusableInflater&, Utils::Oodler&) const;
	};
}
