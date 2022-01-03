#pragma once

#include <XivAlexanderCommon/Utils_NumericStatisticsTracker.h>
#include <XivAlexanderCommon/Utils_ListenerManager.h>
#include "App_Network_Structures.h"

namespace App {
	class XivAlexApp;
}

namespace App::Feature {
	class NetworkTimingHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		NetworkTimingHandler(XivAlexApp& app);
		~NetworkTimingHandler();

		struct CooldownGroup {
			static constexpr uint32_t Id_Gcd = 0x0039;  // 58 in exd, 57 in cooldown struct

			uint32_t Id = 0;
			uint64_t TimestampUs = 0;
			uint64_t DurationUs = UINT64_MAX;
			Utils::NumericStatisticsTracker DriftTrackerUs{ 128, 0 };
		};

		const CooldownGroup& GetCooldownGroup(uint32_t groupId) const;
		Utils::ListenerManager<Implementation, void, const CooldownGroup&, bool> OnCooldownGroupUpdateListener;
		Utils::ListenerManager<Implementation, void, const Network::Structures::XivIpcs::C2S_ActionRequest&> OnActionRequestListener;
	};
}
