#pragma once
#include <xivres/util.on_dtor.h>

namespace Utils {
	class NumericStatisticsTracker;
}

namespace XivAlexander::Misc {
	class IcmpPingTracker {
		struct Implementation;
		friend struct Implementation;

		std::unique_ptr<Implementation> const m_pImpl;

	public:
		IcmpPingTracker();
		~IcmpPingTracker();

		xivres::util::on_dtor Track(const in_addr& source, const in_addr& destination);

		[[nodiscard]] const Utils::NumericStatisticsTracker* GetTrackerUs(const in_addr& source, const in_addr& destination) const;
	};
}
