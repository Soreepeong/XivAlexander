#pragma once

#include <XivAlexanderCommon/Utils/NumericStatisticsTracker.h>
#include <XivAlexanderCommon/Utils/ListenerManager.h>
#include <XivAlexanderCommon/Sqex/Network/Structure.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class NetworkTimingHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		NetworkTimingHandler(Apps::MainApp::App& app);
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
		Utils::ListenerManager<Implementation, void, const Sqex::Network::Structure::XivIpcs::C2S_ActionRequest&> OnActionRequestListener;
	};
}
