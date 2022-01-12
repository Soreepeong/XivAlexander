#include "pch.h"
#include "IcmpPingTracker.h"

#include <XivAlexanderCommon/Utils/NumericStatisticsTracker.h>
#include <XivAlexanderCommon/Utils/Win32/Closeable.h>

#include "Config.h"
#include "Misc/Logger.h"
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

struct XivAlexander::Misc::IcmpPingTracker::Implementation {
	struct SingleTracker;

	const std::shared_ptr<Misc::Logger> Logger;
	const std::shared_ptr<Config> Config;

	std::mutex TrackersMtx;
	std::map<ConnectionPair, std::shared_ptr<SingleTracker>> Trackers;

	struct SingleTracker {
		Misc::IcmpPingTracker& IcmpPingTracker;
		const std::shared_ptr<Utils::NumericStatisticsTracker> Tracker;
		const Utils::Win32::Event ExitEvent;
		const ConnectionPair Pair;
		// needs to be last, as "this" needs to be done initializing
		const Utils::Win32::Thread WorkerThread;

		SingleTracker(Misc::IcmpPingTracker& icmpPingTracker, const ConnectionPair& pair)
			: IcmpPingTracker(icmpPingTracker)
			, Tracker(std::make_shared<Utils::NumericStatisticsTracker>(8, INT64_MAX, 60 * 1000 * 1000))
			, ExitEvent(Utils::Win32::Event::Create())
			, Pair(pair)
			, WorkerThread(std::format(L"XivAlexander::App::Network::IcmpPingTracker({:x})::SingleTracker({:x}: {} <-> {})",
					reinterpret_cast<size_t>(&icmpPingTracker), reinterpret_cast<size_t>(this),
					Utils::ToString(pair.Source), Utils::ToString(pair.Destination)
				), [this]() { Run(); }) {
		}

		~SingleTracker() {
			ExitEvent.Set();
			WorkerThread.Wait();
		}

	private:
		void Run() {
			static constexpr auto SecondToMicrosecondMultiplier = 1000000;

			const auto& logger = IcmpPingTracker.m_pImpl->Logger;
			const auto& config = IcmpPingTracker.m_pImpl->Config;
			try {
				const auto hIcmp = Utils::Win32::Icmp(IcmpCreateFile(), INVALID_HANDLE_VALUE);

				unsigned char sendBuf[32]{};
				unsigned char replyBuf[sizeof(ICMP_ECHO_REPLY) + sizeof sendBuf + 8]{};

				logger->Format(LogCategory::SocketHook,
					config->Runtime.GetLangId(),
					IDS_PINGTRACKER_START,
					Utils::ToString(Pair.Source),
					Utils::ToString(Pair.Destination));
				size_t consecutiveFailureCount = 0;
				do {
					const auto intervalUs = std::max<DWORD>(SecondToMicrosecondMultiplier, static_cast<DWORD>(std::min<uint64_t>(INT32_MAX, Tracker->NextBlankInUs())));
					const auto startUs = Utils::QpcUs();
					const auto ok = IcmpSendEcho2Ex(hIcmp, nullptr, nullptr, nullptr, Pair.Source.s_addr, Pair.Destination.s_addr, sendBuf, sizeof sendBuf, nullptr, replyBuf, sizeof replyBuf, 5000);  // wait up to 5s
					const auto endUs = Utils::QpcUs();
					const auto latencyUs = endUs - startUs;
					auto waitTimeUs = static_cast<DWORD>(intervalUs - latencyUs);

					if (ok) {
						consecutiveFailureCount = 0;

						const auto latest = Tracker->Latest();
						Tracker->AddValue(latencyUs);

						// if ping changes by more than 10%, then ping again to confirm
						if (latencyUs > 0 && 100 * std::abs(latest - latencyUs) / latencyUs >= 10)
							waitTimeUs = 1;
					} else
						consecutiveFailureCount++;

					if (ExitEvent.Wait(waitTimeUs / 1000) != WAIT_TIMEOUT) {
						logger->Format(LogCategory::SocketHook,
							config->Runtime.GetLangId(),
							IDS_PINGTRACKER_END,
							Utils::ToString(Pair.Source),
							Utils::ToString(Pair.Destination));
						break;
					}

					if (consecutiveFailureCount >= 10) {
						logger->Format<LogLevel::Warning>(LogCategory::SocketHook,
							config->Runtime.GetLangId(),
							IDS_PINGTRACKER_END_FAILURE_COUNT,
							Utils::ToString(Pair.Source),
							Utils::ToString(Pair.Destination),
							10);
						break;
					}
				} while (true);
			} catch (const std::exception& e) {
				logger->Format<LogLevel::Error>(LogCategory::SocketHook,
					config->Runtime.GetLangId(),
					IDS_PINGTRACKER_END_ERROR,
					Utils::ToString(Pair.Source),
					Utils::ToString(Pair.Destination),
					e.what());
			}
		}
	};

	Implementation()
		: Logger(Misc::Logger::Acquire())
		, Config(Config::Acquire()) {
	}
};

XivAlexander::Misc::IcmpPingTracker::IcmpPingTracker()
	: m_pImpl(std::make_unique<Implementation>()) {
}

XivAlexander::Misc::IcmpPingTracker::~IcmpPingTracker() = default;

Utils::CallOnDestruction XivAlexander::Misc::IcmpPingTracker::Track(const in_addr& source, const in_addr& destination) {
	const auto pair = ConnectionPair{source, destination};
	std::lock_guard _lock(m_pImpl->TrackersMtx);
	if (const auto it = m_pImpl->Trackers.find(pair); it == m_pImpl->Trackers.end())
		m_pImpl->Trackers.emplace(pair, std::make_shared<Implementation::SingleTracker>(*this, pair));
	return Utils::CallOnDestruction([this, pair]() {
		m_pImpl->Trackers.erase(pair);
	});
}

const Utils::NumericStatisticsTracker* XivAlexander::Misc::IcmpPingTracker::GetTrackerUs(const in_addr& source, const in_addr& destination) const {
	const auto pair = ConnectionPair{source, destination};
	if (const auto it = m_pImpl->Trackers.find(pair); it != m_pImpl->Trackers.end())
		return it->second->Tracker.get();
	return nullptr;
}
