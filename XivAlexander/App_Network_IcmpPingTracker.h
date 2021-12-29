#pragma once
#include "XivAlexanderCommon/Utils_CallOnDestruction.h"

namespace Utils {
	class NumericStatisticsTracker;
}

namespace App::Network {
	class IcmpPingTracker {
		struct Implementation;
		friend struct Implementation;

		std::unique_ptr<Implementation> const m_pImpl;

	public:
		IcmpPingTracker();
		~IcmpPingTracker();

		Utils::CallOnDestruction Track(const in_addr& source, const in_addr& destination);

		[[nodiscard]] const Utils::NumericStatisticsTracker* GetTrackerUs(const in_addr& source, const in_addr& destination) const;
	};
}
