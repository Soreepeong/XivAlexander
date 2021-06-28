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

class App::Network::IcmpPingTracker::Implementation {
public:
	class SingleTracker;

	std::mutex m_trackersMapLock;
	std::map<ConnectionPair, std::shared_ptr<SingleTracker>> m_trackersByAddress;

	class SingleTracker {
	public:
		const std::shared_ptr<Utils::NumericStatisticsTracker> Tracker;

	private:
		const Utils::Win32::Closeable::Handle m_hExitEvent;
		const ConnectionPair m_pair;
		std::thread m_workerThread;

	public:
		SingleTracker(const ConnectionPair& pair)
			: Tracker(std::make_shared<Utils::NumericStatisticsTracker>(8, INT64_MAX, 60 * 1000))
			, m_hExitEvent(CreateEventW(nullptr, true, false, nullptr), nullptr)
			, m_pair(pair)
			, m_workerThread([this]() {Run(); }) {
		}

		~SingleTracker() {
			SetEvent(m_hExitEvent);
			m_workerThread.join();
		}

	private:
		void Run() {
			const Utils::Win32::Closeable::Base<HANDLE, IcmpCloseHandle> hIcmp(IcmpCreateFile(), INVALID_HANDLE_VALUE);

			unsigned char sendBuf[32]{};
			unsigned char replyBuf[sizeof(ICMP_ECHO_REPLY) + sizeof sendBuf + 8]{};
			
			do {
				const auto interval = std::max<DWORD>(1000, static_cast<DWORD>(std::min<uint64_t>(INT32_MAX, Tracker->NextBlankIn())));
				const auto startTime = Utils::GetHighPerformanceCounter();
				const auto ok = IcmpSendEcho2Ex(hIcmp, nullptr, nullptr, nullptr, m_pair.Source.s_addr, m_pair.Destination.s_addr, sendBuf, sizeof sendBuf, nullptr, replyBuf, sizeof replyBuf, interval);
				const auto endTime = Utils::GetHighPerformanceCounter();
				const auto latency = endTime - startTime;
				const auto waitTime = static_cast<DWORD>(interval - latency);
				if (ok) {
					Tracker->AddValue(latency);
					Misc::Logger::GetLogger().Format(LogCategory::SocketHook, 
						"Ping {} -> {}: {}ms, mean={}+{}ms, median={}ms ({}ms ~ {}ms), next check in {}ms",
						Utils::ToString(m_pair.Source),
						Utils::ToString(m_pair.Destination),
						latency, Tracker->Mean(), Tracker->Deviation(), Tracker->Median(), Tracker->Min(), Tracker->Max(), waitTime);
				} else
					Misc::Logger::GetLogger().Format(LogCategory::SocketHook,
						"Ping {} -> {}: failure, next check in {}ms",
						Utils::ToString(m_pair.Source),
						Utils::ToString(m_pair.Destination),
						waitTime);
				if (WaitForSingleObject(m_hExitEvent, waitTime) != WAIT_TIMEOUT)
					break;
			} while (true);
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook,
				"Ping {} -> {}: track end",
				Utils::ToString(m_pair.Source),
				Utils::ToString(m_pair.Destination));
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
	ConnectionPair pair{ source, destination };
	std::lock_guard _lock(m_impl->m_trackersMapLock);
	if (const auto it = m_impl->m_trackersByAddress.find(pair); it == m_impl->m_trackersByAddress.end())
		m_impl->m_trackersByAddress.emplace(pair, std::make_shared<Implementation::SingleTracker>(pair));
	return Utils::CallOnDestruction([this, pair]() {
		m_impl->m_trackersByAddress.erase(pair);
		});
}

const Utils::NumericStatisticsTracker* App::Network::IcmpPingTracker::GetTracker(const in_addr& source, const in_addr& destination) const {
	ConnectionPair pair{ source, destination };
	if (const auto it = m_impl->m_trackersByAddress.find(pair); it != m_impl->m_trackersByAddress.end())
		return it->second->Tracker.get();
	return nullptr;
}
