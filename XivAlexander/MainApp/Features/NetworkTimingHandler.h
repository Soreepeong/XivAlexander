#pragma once

#include "Utils/NumericStatisticsTracker.h"
#include <xivres/util.listener_manager.h>
#include <xivres/network.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Features {
	class NetworkTimingHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		NetworkTimingHandler(App& app);
		~NetworkTimingHandler();

		struct CooldownGroup {
			static constexpr uint32_t Id_Gcd = 0x0039;  // 58 in exd, 57 in cooldown struct

			uint32_t Id = 0;
			uint64_t TimestampUs = 0;
			uint64_t DurationUs = UINT64_MAX;
			Utils::NumericStatisticsTracker DriftTrackerUs{ 128, 0 };
		};

		const CooldownGroup& GetCooldownGroup(uint32_t groupId) const;
		xivres::util::listener_manager<Implementation, void, const CooldownGroup&, bool> OnCooldownGroupUpdateListener;
		xivres::util::listener_manager<Implementation, void, const xivres::network::ipcs::C2S_ActionRequest&> OnActionRequestListener;
	};
}
