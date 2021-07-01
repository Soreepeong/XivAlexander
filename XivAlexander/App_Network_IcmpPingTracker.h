#pragma once
namespace App::Network {
	class IcmpPingTracker {
		class Implementation;
		friend class Implementation;

		std::unique_ptr<Implementation> const m_pImpl;

	public:
		IcmpPingTracker();
		~IcmpPingTracker();

		Utils::CallOnDestruction Track(const in_addr& source, const in_addr& destination);

		[[nodiscard]] const Utils::NumericStatisticsTracker* GetTracker(const in_addr& source, const in_addr& destination) const;
	};
}