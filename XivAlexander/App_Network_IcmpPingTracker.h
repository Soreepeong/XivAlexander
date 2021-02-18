#pragma once
namespace App::Network {
	class IcmpPingTracker {
		class Implementation;
		friend class Implementation;

		std::unique_ptr<Implementation> const m_impl;

	public:
		IcmpPingTracker();

		static IcmpPingTracker& GetInstance();
		static void Cleanup();

		Utils::CallOnDestruction Track(const in_addr& source, const in_addr& destination);

		static
		uint64_t GetMedianLatency(const in_addr& source, const in_addr& destination);
	};
}
