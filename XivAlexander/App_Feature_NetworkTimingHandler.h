#pragma once

#include <XivAlexanderCommon/Utils_NumericStatisticsTracker.h>
#include <XivAlexanderCommon/Utils_ListenerManager.h>

namespace App {
	namespace Network {
		class SocketHook;
	}
}

namespace App::Feature {
	class NetworkTimingHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		NetworkTimingHandler(Network::SocketHook* socketHook);
		~NetworkTimingHandler();

		struct CooldownGroup {
			static constexpr uint32_t Id_Gcd = 0x0039;  // 58 in exd, 57 in cooldown struct

			uint32_t Id = 0;
			uint32_t LastActionId = 0;
			uint64_t TimestampUs = 0;
			uint64_t DurationUs = UINT64_MAX;
			Utils::NumericStatisticsTracker DriftTrackerUs{ 128, 0 };
		};

		const CooldownGroup& GetCooldownGroup(uint32_t groupId) const;
		Utils::ListenerManager<Implementation, void, const CooldownGroup&, bool> OnCooldownGroupUpdateListener;
	};
}
