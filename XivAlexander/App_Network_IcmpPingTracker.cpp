#include "pch.h"
#include "App_Network_IcmpPingTracker.h"
#include "resource.h"

namespace Utils::Win32 {
	using Icmp = Closeable<HANDLE, IcmpCloseHandle>;
}

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
	const std::shared_ptr<Config> m_config;

	std::mutex m_trackersMapLock;
	std::map<ConnectionPair, std::shared_ptr<SingleTracker>> m_trackersByAddress;

	class SingleTracker {
		IcmpPingTracker* const m_icmpPingTracker;
	public:
		const std::shared_ptr<Utils::NumericStatisticsTracker> Tracker;

	private:
		const Utils::Win32::Event m_hExitEvent;
		const ConnectionPair m_pair;

		// needs to be last, as "this" needs to be done initializing
		const Utils::Win32::Thread m_hWorkerThread;

	public:
		SingleTracker(IcmpPingTracker* icmpPingTracker, const ConnectionPair& pair)
			: m_icmpPingTracker(icmpPingTracker)
			, Tracker(std::make_shared<Utils::NumericStatisticsTracker>(8, INT64_MAX, 60 * 1000))
			, m_hExitEvent(Utils::Win32::Event::Create())
			, m_pair(pair)
			, m_hWorkerThread(std::format(L"XivAlexander::App::Network::IcmpPingTracker({:x})::SingleTracker({:x}: {} <-> {})",
				reinterpret_cast<size_t>(icmpPingTracker), reinterpret_cast<size_t>(this),
				Utils::ToString(pair.Source), Utils::ToString(pair.Destination)
			), [this]() { Run(); }) {
		}

		~SingleTracker() {
			m_hExitEvent.Set();
			m_hWorkerThread.Wait();
		}

	private:
		void Run() {
			const auto& logger = m_icmpPingTracker->m_pImpl->m_logger;
			const auto& config = m_icmpPingTracker->m_pImpl->m_config;
			try {
				const auto hIcmp = Utils::Win32::Icmp(IcmpCreateFile(), INVALID_HANDLE_VALUE);

				unsigned char sendBuf[32]{};
				unsigned char replyBuf[sizeof(ICMP_ECHO_REPLY) + sizeof sendBuf + 8]{};

				logger->Format(LogCategory::SocketHook,
					config->Runtime.GetLangId(),
					IDS_PINGTRACKER_START,
					Utils::ToString(m_pair.Source),
					Utils::ToString(m_pair.Destination));
				size_t consecutiveFailureCount = 0;
				do {
					const auto interval = std::max<DWORD>(1000, static_cast<DWORD>(std::min<uint64_t>(INT32_MAX, Tracker->NextBlankIn())));
					const auto startTime = Utils::GetHighPerformanceCounter();
					const auto ok = IcmpSendEcho2Ex(hIcmp, nullptr, nullptr, nullptr, m_pair.Source.s_addr, m_pair.Destination.s_addr, sendBuf, sizeof sendBuf, nullptr, replyBuf, sizeof replyBuf, interval);
					const auto endTime = Utils::GetHighPerformanceCounter();
					const auto latency = endTime - startTime;
					auto waitTime = static_cast<DWORD>(interval - latency);

					if (ok) {
						consecutiveFailureCount = 0;

						const auto latest = Tracker->Latest();
						Tracker->AddValue(latency);

						// if ping changes by more than 10%, then ping again to confirm
						if (latency > 0 && 100 * std::abs(latest - latency) / latency >= 10)
							waitTime = 1;
					} else
						consecutiveFailureCount++;

					if (m_hExitEvent.Wait(waitTime) != WAIT_TIMEOUT) {
						logger->Format(LogCategory::SocketHook,
							config->Runtime.GetLangId(),
							IDS_PINGTRACKER_END,
							Utils::ToString(m_pair.Source),
							Utils::ToString(m_pair.Destination));
						break;
					}

					if (consecutiveFailureCount >= 10) {
						logger->Format<LogLevel::Warning>(LogCategory::SocketHook,
							config->Runtime.GetLangId(),
							IDS_PINGTRACKER_END_FAILURE_COUNT,
							Utils::ToString(m_pair.Source),
							Utils::ToString(m_pair.Destination),
							10);
						break;
					}
				} while (true);
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Error>(LogCategory::SocketHook,
					config->Runtime.GetLangId(),
					IDS_PINGTRACKER_END_ERROR,
					Utils::ToString(m_pair.Source),
					Utils::ToString(m_pair.Destination),
					e.what());
			}
		}
	};

	Implementation()
		: m_logger(Misc::Logger::Acquire())
		, m_config(Config::Acquire()) {
	}
};

App::Network::IcmpPingTracker::IcmpPingTracker()
	: m_pImpl(std::make_unique<Implementation>()) {
}

App::Network::IcmpPingTracker::~IcmpPingTracker() = default;

Utils::CallOnDestruction App::Network::IcmpPingTracker::Track(const in_addr & source, const in_addr & destination) {
	const auto pair = ConnectionPair{ source, destination };
	std::lock_guard _lock(m_pImpl->m_trackersMapLock);
	if (const auto it = m_pImpl->m_trackersByAddress.find(pair); it == m_pImpl->m_trackersByAddress.end())
		m_pImpl->m_trackersByAddress.emplace(pair, std::make_shared<Implementation::SingleTracker>(this, pair));
	return Utils::CallOnDestruction([this, pair]() {
		m_pImpl->m_trackersByAddress.erase(pair);
	});
}

const Utils::NumericStatisticsTracker* App::Network::IcmpPingTracker::GetTracker(const in_addr & source, const in_addr & destination) const {
	const auto pair = ConnectionPair{ source, destination };
	if (const auto it = m_pImpl->m_trackersByAddress.find(pair); it != m_pImpl->m_trackersByAddress.end())
		return it->second->Tracker.get();
	return nullptr;
}
