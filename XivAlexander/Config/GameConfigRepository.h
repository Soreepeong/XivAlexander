#pragma once

#include <xivres/common.h>

#include "PatchInstruction.h"
#include "BaseConfigRepository.h"

namespace XivAlexander {
	class GameConfigRepository : public BaseConfigRepository {
	public:
		constexpr static uint16_t InvalidIpcType = 0x93DB;

	private:
		friend class Config;
		using BaseConfigRepository::BaseConfigRepository;

	public:
		ConfigItem<std::vector<PatchInstruction>> PatchCode{this, "PatchCode", std::vector<PatchInstruction>()};

		ConfigItem<bool> Common_UseOodleTcp{this, "Common_UseOodleTcp", true};

		// Make the program consume all network connections by default.
		ConfigItem<std::string> Server_IpRange{this, "Server_IpRange", std::string("0.0.0.0/0")};
		ConfigItem<std::string> Server_PortRange{this, "Server_PortRange", std::string("54992-54994, 55006-55007, 55021-55040")};

		// Set defaults so that the values will never be a valid IPC code.
		// Assumes structure doesn't change too often.
		// Will be loaded from configuration file on initialization.
		ConfigItem<uint16_t> S2C_ActionEffects[5]{
			{this, "S2C_ActionEffect01", InvalidIpcType},
			{this, "S2C_ActionEffect08", InvalidIpcType},
			{this, "S2C_ActionEffect16", InvalidIpcType},
			{this, "S2C_ActionEffect24", InvalidIpcType},
			{this, "S2C_ActionEffect32", InvalidIpcType},
		};
		ConfigItem<uint16_t> S2C_ActorControl{this, "S2C_ActorControl", InvalidIpcType};
		ConfigItem<uint16_t> S2C_ActorControlSelf{this, "S2C_ActorControlSelf", InvalidIpcType};
		ConfigItem<uint16_t> S2C_ActorCast{this, "S2C_ActorCast", InvalidIpcType};
		ConfigItem<uint16_t> C2S_ActionRequest[2]{
			{this, "C2S_ActionRequest", InvalidIpcType},
			{this, "C2S_ActionRequestGroundTargeted", InvalidIpcType},
		};
	};
}
