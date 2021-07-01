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

class App::Network::IcmpPingTracker::Implementation {
public:
	class SingleTracker;

	const std::shared_ptr<Misc::Logger> m_logger;

	std::mutex m_trackersMapLock;
	std::map<ConnectionPair, std::shared_ptr<SingleTracker>> m_trackersByAddress;

	class SingleTracker {
		IcmpPingTracker* const m_icmpPingTracker;
	public:
		const std::shared_ptr<Utils::NumericStatisticsTracker> Tracker;

	private:
		const Utils::Win32::Closeable::Handle m_hExitEvent;
		const ConnectionPair m_pair;
		std::thread m_workerThread;

	public:
		SingleTracker(IcmpPingTracker* icmpPingTracker, const ConnectionPair& pair)
			: m_icmpPingTracker(icmpPingTracker)
			, Tracker(std::make_shared<Utils::NumericStatisticsTracker>(8, INT64_MAX, 60 * 1000))
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
			const auto& logger = m_icmpPingTracker->m_pImpl->m_logger;
			try {
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
						logger->Format(LogCategory::SocketHook,
							"Ping {} -> {}: {}ms, mean={}+{}ms, median={}ms ({}ms ~ {}ms), next check in {}ms",
							Utils::ToString(m_pair.Source),
							Utils::ToString(m_pair.Destination),
							latency, Tracker->Mean(), Tracker->Deviation(), Tracker->Median(), Tracker->Min(), Tracker->Max(), waitTime);
					} else
						logger->Format(LogCategory::SocketHook,
							"Ping {} -> {}: failure, next check in {}ms",
							Utils::ToString(m_pair.Source),
							Utils::ToString(m_pair.Destination),
							waitTime);
					if (WaitForSingleObject(m_hExitEvent, waitTime) != WAIT_TIMEOUT)
						break;
				} while (true);
				
				logger->Format(LogCategory::SocketHook,
					"Ping {} -> {}: track end",
					Utils::ToString(m_pair.Source),
					Utils::ToString(m_pair.Destination));
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Error>(LogCategory::SocketHook,
					"Ping {} -> {}: track end due to error: %s",
					Utils::ToString(m_pair.Source),
					Utils::ToString(m_pair.Destination),
					e.what());
			}
		}
	};

	Implementation()
		: m_logger(Misc::Logger::Acquire()) {
	}
};

App::Network::IcmpPingTracker::IcmpPingTracker()
	: m_pImpl(std::make_unique<Implementation>()) {
}

App::Network::IcmpPingTracker::~IcmpPingTracker() = default;

Utils::CallOnDestruction App::Network::IcmpPingTracker::Track(const in_addr& source, const in_addr& destination) {
	const auto pair = ConnectionPair{ source, destination };
	std::lock_guard _lock(m_pImpl->m_trackersMapLock);
	if (const auto it = m_pImpl->m_trackersByAddress.find(pair); it == m_pImpl->m_trackersByAddress.end())
		m_pImpl->m_trackersByAddress.emplace(pair, std::make_shared<Implementation::SingleTracker>(this, pair));
	return Utils::CallOnDestruction([this, pair]() {
		m_pImpl->m_trackersByAddress.erase(pair);
		});
}

const Utils::NumericStatisticsTracker* App::Network::IcmpPingTracker::GetTracker(const in_addr& source, const in_addr& destination) const {
	const auto pair = ConnectionPair{ source, destination };
	if (const auto it = m_pImpl->m_trackersByAddress.find(pair); it != m_pImpl->m_trackersByAddress.end())
		return it->second->Tracker.get();
	return nullptr;
}
