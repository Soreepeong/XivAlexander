#include "pch.h"
#include "App_Network_IcmpPingTracker.h"

struct ConnectionPair {
	in_addr Source;
	in_addr Destination;

	bool operator <(const ConnectionPair& r) const {
		return (Source.S_un.S_addr < r.Source.S_un.S_addr)
			|| (Source.S_un.S_addr == r.Source.S_un.S_addr && Destination.S_un.S_addr < r.Destination.S_un.S_addr);
	}
};

static std::unique_ptr<App::Network::IcmpPingTracker> s_instance;
static std::mutex s_latestKnownLatencyLock;
static std::map<ConnectionPair, std::vector<uint64_t>> s_latestKnownLatency;
static const int LatencyTrackCount = 16;

class App::Network::IcmpPingTracker::Implementation {
public:
	class SingleTracker;

	std::mutex m_trackersMapLock;
	uint64_t m_nCounter = 0;
	std::map<uint64_t, std::shared_ptr<SingleTracker>> m_trackers;
	std::map<ConnectionPair, std::weak_ptr<SingleTracker>> m_trackersByAddress;

	class SingleTracker {
		struct ThreadInfo {
			HANDLE hExitTriggerEvent;
			ConnectionPair Pair;
		};

		Utils::Win32Handle<> m_hExitTriggerEventForThread;
		ThreadInfo* const m_threadInfoForCreation;
		Utils::Win32Handle<> const m_hThread;
		HANDLE const m_hExitTriggerEvent;

	public:
		SingleTracker(const ConnectionPair& pair)
			: m_hExitTriggerEventForThread(CreateEventW(nullptr, false, false, nullptr))
			, m_threadInfoForCreation(new ThreadInfo{ m_hExitTriggerEventForThread, pair })
			, m_hThread(CreateThread(nullptr, 0, SingleTracker::Run, m_threadInfoForCreation, 0, nullptr))
			, m_hExitTriggerEvent(m_hExitTriggerEventForThread) {
			m_hExitTriggerEventForThread.Detach();
		}

		~SingleTracker() {
			SetEvent(m_hExitTriggerEvent);
			WaitForSingleObject(m_hThread, INFINITE);
		}

		static DWORD Run(void* p) {
			const ThreadInfo info = *reinterpret_cast<ThreadInfo*>(p);
			delete p;

			Utils::Win32Handle<HANDLE, IcmpCloseHandle> hIcmp(IcmpCreateFile());

			unsigned char SendBuf[32]{};
			unsigned char ReplyBuf[sizeof(ICMP_ECHO_REPLY) + sizeof SendBuf + 8]{};

			DWORD waitTime;
			size_t successCount = 0;
			do {
				const auto interval = successCount > 16 ? 10000 : 1000;
				const auto startTime = Utils::GetHighPerformanceCounter();
				const auto ok = IcmpSendEcho2Ex(hIcmp, nullptr, nullptr, nullptr, info.Pair.Source.S_un.S_addr, info.Pair.Destination.S_un.S_addr, SendBuf, sizeof SendBuf, nullptr, ReplyBuf, sizeof ReplyBuf, interval);
				const auto endTime = Utils::GetHighPerformanceCounter();
				const auto latency = endTime - startTime;
				waitTime = static_cast<DWORD>(interval - latency);
				if (ok) {
					successCount++;
					std::lock_guard<std::mutex> _lock(s_latestKnownLatencyLock);
					auto& list = s_latestKnownLatency[info.Pair];
					const auto newPos = list.empty() ? 0 : std::upper_bound(list.begin(), list.end(), latency) - list.begin();
					list.insert(list.begin() + newPos, latency);
					if (list.size() > LatencyTrackCount) {
						if (newPos >= LatencyTrackCount / 2)
							list.pop_back();
						else
							list.erase(list.begin());
					}
					Misc::Logger::GetLogger().Format("Ping %d.%d.%d.%d -> %d.%d.%d.%d: ok, %llums, median=%llums (%llums ~ %llums, %lld %s), next check in %ums",
						info.Pair.Source.S_un.S_un_b.s_b1,
						info.Pair.Source.S_un.S_un_b.s_b2,
						info.Pair.Source.S_un.S_un_b.s_b3,
						info.Pair.Source.S_un.S_un_b.s_b4,
						info.Pair.Destination.S_un.S_un_b.s_b1,
						info.Pair.Destination.S_un.S_un_b.s_b2,
						info.Pair.Destination.S_un.S_un_b.s_b3,
						info.Pair.Destination.S_un.S_un_b.s_b4,
						latency,
						std::max(1ULL, list.size() % 2 ? list[list.size() / 2] : (list[list.size() / 2] + list[list.size() / 2 - 1]) / 2),
						list.front(), list.back(), list.size(), list.size() > 1 ? "items" : "item",
						waitTime);
				} else
					Misc::Logger::GetLogger().Format("Ping %d.%d.%d.%d -> %d.%d.%d.%d: failure",
						info.Pair.Source.S_un.S_un_b.s_b1,
						info.Pair.Source.S_un.S_un_b.s_b2,
						info.Pair.Source.S_un.S_un_b.s_b3,
						info.Pair.Source.S_un.S_un_b.s_b4,
						info.Pair.Destination.S_un.S_un_b.s_b1,
						info.Pair.Destination.S_un.S_un_b.s_b2,
						info.Pair.Destination.S_un.S_un_b.s_b3,
						info.Pair.Destination.S_un.S_un_b.s_b4);
			} while (WaitForSingleObject(info.hExitTriggerEvent, waitTime) == WAIT_TIMEOUT);
			Misc::Logger::GetLogger().Format("Ping %d.%d.%d.%d -> %d.%d.%d.%d: stop track",
				info.Pair.Source.S_un.S_un_b.s_b1,
				info.Pair.Source.S_un.S_un_b.s_b2,
				info.Pair.Source.S_un.S_un_b.s_b3,
				info.Pair.Source.S_un.S_un_b.s_b4,
				info.Pair.Destination.S_un.S_un_b.s_b1,
				info.Pair.Destination.S_un.S_un_b.s_b2,
				info.Pair.Destination.S_un.S_un_b.s_b3,
				info.Pair.Destination.S_un.S_un_b.s_b4);
			CloseHandle(info.hExitTriggerEvent);
			return 0;
		}
	};
};

App::Network::IcmpPingTracker::IcmpPingTracker()
	: m_impl(std::make_unique<Implementation>()) {

}

App::Network::IcmpPingTracker& App::Network::IcmpPingTracker::GetInstance() {
	if (!s_instance)
		s_instance = std::make_unique<IcmpPingTracker>();
	return *s_instance;
}

void App::Network::IcmpPingTracker::Cleanup() {
	s_instance = nullptr;
}

Utils::CallOnDestruction App::Network::IcmpPingTracker::Track(const in_addr& source, const in_addr& destination) {
	std::lock_guard<std::mutex> _lock(m_impl->m_trackersMapLock);
	ConnectionPair pair{ source, destination };
	const auto counter = m_impl->m_nCounter++;
	std::shared_ptr<Implementation::SingleTracker> pTracker;
	try {
		pTracker = m_impl->m_trackersByAddress.at(pair).lock();
	} catch (std::out_of_range&) {
	}
	if (!pTracker)
		pTracker = std::make_shared<Implementation::SingleTracker>(pair);
	m_impl->m_trackersByAddress.emplace(pair, pTracker);
	m_impl->m_trackers.emplace(counter, pTracker);
	return Utils::CallOnDestruction([this, counter]() {
		m_impl->m_trackers.erase(counter);
		});
}

uint64_t App::Network::IcmpPingTracker::GetMedianLatency(const in_addr& source, const in_addr& destination) {
	ConnectionPair pair{ source, destination };
	try {
		std::lock_guard<std::mutex> _lock(s_latestKnownLatencyLock);
		const auto& list = s_latestKnownLatency.at(pair);
		if (list.empty())
			return 0;
		return std::max(1ULL, list.size() % 2 ? list[list.size() / 2] : (list[list.size() / 2] + list[list.size() / 2 - 1]) / 2);
	} catch (std::out_of_range&) {
		return 0;
	}
}
