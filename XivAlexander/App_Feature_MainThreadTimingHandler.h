#pragma once

namespace App {
	class XivAlexApp;
}

namespace Utils {
	class NumericStatisticsTracker;
}

namespace App::Feature {
	class MainThreadTimingHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		MainThreadTimingHandler(XivAlexApp& app);
		~MainThreadTimingHandler();

		[[nodiscard]] const Utils::NumericStatisticsTracker& GetMessagePumpIntervalTrackerUs() const;
		[[nodiscard]] const Utils::NumericStatisticsTracker& GetRenderTimeTakenTrackerUs() const;
		[[nodiscard]] const Utils::NumericStatisticsTracker& GetSocketCallDelayTrackerUs() const;

		void GuaranteePumpBeginCounterIn(int64_t nextInUs);
		void GuaranteePumpBeginCounterAt(int64_t counterUs);
	};
}
