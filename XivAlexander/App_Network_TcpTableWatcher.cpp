#include "pch.h"
#include "App_Network_TcpTableWatcher.h"

class App::Network::TcpTableWatcher::Implementation {
public:
	struct ConnectionIdentifier {
		uint32_t localAddr;
		uint32_t localPort;
		uint32_t remoteAddr;
		uint32_t remotePort;

		bool operator <(const ConnectionIdentifier& rhs) const {
			if (localAddr != rhs.localAddr)
				return localAddr < rhs.localAddr;
			if (localPort != rhs.localPort)
				return localPort < rhs.localPort;
			if (remoteAddr != rhs.remoteAddr)
				return remoteAddr < rhs.remoteAddr;
			if (remotePort != rhs.remotePort)
				return remotePort < rhs.remotePort;
			return false;
		}
	};

	std::mutex m_dataLock;
	std::map<ConnectionIdentifier, TCP_ESTATS_PATH_ROD_v0> m_data;

	Utils::Win32Handle<> const m_hExitEvent;
	Utils::Win32Handle<> const m_hTrackerThread;

	Implementation()
		: m_hExitEvent(CreateEventW(nullptr, false, false, nullptr))
		, m_hTrackerThread(CreateThread(nullptr, 0, [](void* pThis) {return reinterpret_cast<Implementation*>(pThis)->TrackerThread(); }, this, 0, nullptr)) {

	}

	~Implementation() {
		SetEvent(m_hExitEvent);
		WaitForSingleObject(m_hTrackerThread, INFINITE);
	}

	DWORD TrackerThread() {
		TCP_ESTATS_PATH_ROD_v0 pathRod;
		TCP_ESTATS_PATH_RW_v0 pathRw;
		DWORD retVal;
		std::vector<char> buffer;
		buffer.resize(1);

		while (WaitForSingleObject(m_hExitEvent, 5000) == WAIT_TIMEOUT) {
			DWORD len = static_cast<DWORD>(buffer.size());

			while (ERROR_INSUFFICIENT_BUFFER == (retVal = GetTcpTable2(reinterpret_cast<PMIB_TCPTABLE2>(&buffer[0]), &len, TRUE)))
				buffer.resize(len);

			if (retVal != NO_ERROR) {
				Misc::Logger::GetLogger().Format<Misc::Logger::LogLevel::Warning>(u8"Failed to get TCP table: %d / %d", retVal, GetLastError());
				continue;
			}

			std::lock_guard<decltype(m_dataLock)> _lock(m_dataLock);
			m_data.clear();
			const auto pTcpTable = reinterpret_cast<PMIB_TCPTABLE2>(&buffer[0]);
			for (size_t i = 0; i < pTcpTable->dwNumEntries; ++i) {
				const auto &item = pTcpTable->table[i];
				if (item.dwState != MIB_TCP_STATE_ESTAB 
					|| item.dwOwningPid != GetCurrentProcessId())
					continue;

				if (NO_ERROR != (retVal = GetPerTcpConnectionEStats(reinterpret_cast<PMIB_TCPROW>(&pTcpTable->table[i]), TcpConnectionEstatsPath, (PUCHAR)&pathRw, 0, sizeof(TCP_ESTATS_PATH_RW_v0), nullptr, 0, 0, reinterpret_cast<PUCHAR>(&pathRod), 0, sizeof pathRod))) {
					continue;
				}

				if (!pathRw.EnableCollection) {
					continue;
				}

				m_data.insert_or_assign({ item.dwLocalAddr, item.dwLocalPort, item.dwRemoteAddr, item.dwRemotePort }, pathRod);
			}
		}
		return 0;
	}
};

App::Network::TcpTableWatcher::TcpTableWatcher()
	: m_pImpl(std::make_unique<Implementation>()){
}

App::Network::TcpTableWatcher::~TcpTableWatcher() {
}

static std::unique_ptr<App::Network::TcpTableWatcher> s_instance;
App::Network::TcpTableWatcher& App::Network::TcpTableWatcher::GetInstance() {
	if (!s_instance)
		s_instance = std::make_unique<TcpTableWatcher>();
	return *s_instance;
}

void App::Network::TcpTableWatcher::Cleanup() {
	s_instance = nullptr;
}

int App::Network::TcpTableWatcher::GetSmoothedRtt(uint32_t localAddr, uint32_t localPort, uint32_t remoteAddr, uint32_t remotePort) const {
	const auto it = m_pImpl->m_data.find({ localAddr, localPort, remoteAddr, remotePort });
	if (it == m_pImpl->m_data.end())
		return -1;
	return it->second.SmoothedRtt;
}
