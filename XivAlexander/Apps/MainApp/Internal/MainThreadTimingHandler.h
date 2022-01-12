#pragma once

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace Utils {
	class NumericStatisticsTracker;
}

namespace XivAlexander::Apps::MainApp::Internal {
	class MainThreadTimingHandler {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		MainThreadTimingHandler(Apps::MainApp::App& app);
		~MainThreadTimingHandler();

		[[nodiscard]] const Utils::NumericStatisticsTracker& GetMessagePumpIntervalTrackerUs() const;

		void GuaranteePumpBeginCounterIn(int64_t nextInUs);
		void GuaranteePumpBeginCounterAt(int64_t counterUs);
	};
}
