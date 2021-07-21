#include "pch.h"
#include "App_XivAlexApp.h"
#include "App_Network_SocketHook.h"
#include "App_Network_IcmpPingTracker.h"
#include "App_Network_Structures.h"

class SingleStream {
public:
	bool m_ending = false;
	bool m_closed = false;

	std::vector<uint8_t> m_pending;
	size_t m_pendingStartPos = 0;

	SingleStream() = default;
	~SingleStream() = default;

	void Write(const void* buf, size_t length) {
		const auto uint8buf = static_cast<const uint8_t*>(buf);
		m_pending.insert(m_pending.end(), uint8buf, uint8buf + length);
	}

	size_t Peek(uint8_t* buf, size_t maxlen) const {
		const auto len = static_cast<int>(std::min({
			static_cast<size_t>(1048576),
			maxlen,
			m_pending.size() - m_pendingStartPos,
			}));
		memcpy(buf, &m_pending[m_pendingStartPos], len);
		return len;
	}

	void Consume(size_t length) {
		m_pendingStartPos += length;
		if (m_pendingStartPos == m_pending.size()) {
			m_pending.resize(0);
			m_pendingStartPos = 0;
		}
	}

	size_t Read(uint8_t* buf, size_t maxlen) {
		const auto len = Peek(buf, maxlen);
		Consume(len);
		return len;
	}

	[[nodiscard]] size_t Available() const {
		return m_pending.size() - m_pendingStartPos;
	}

	static void ProxyAvailableData(SingleStream& source, SingleStream& target, const std::function<void(const App::Network::Structures::FFXIVBundle* packet, SingleStream& target)>& processor, App::Misc::Logger* logger) {
		using namespace App::Network::Structures;

		const auto availableLength = source.Available();
		if (!availableLength)
			return;

		std::vector<uint8_t> discardedBytes;
		std::vector<uint8_t> buf;
		buf.resize(availableLength);
		source.Peek(&buf[0], buf.size());
		while (!buf.empty()) {
			if (const auto possibleMagicOffset = FFXIVBundle::FindPossibleBundleIndex(&buf[0], buf.size())) {
				source.Consume(possibleMagicOffset);
				target.Write(&buf[0], possibleMagicOffset);
				discardedBytes.insert(discardedBytes.end(), buf.begin(), buf.begin() + possibleMagicOffset);
				buf.erase(buf.begin(), buf.begin() + possibleMagicOffset);
			}

			// Incomplete header
			if (buf.size() < GamePacketHeaderSize)
				return;

			const FFXIVBundle* pGamePacket = reinterpret_cast<FFXIVBundle*>(&buf[0]);

			// Incomplete data
			if (buf.size() < pGamePacket->TotalLength)
				return;

			processor(pGamePacket, target);

			source.Consume(pGamePacket->TotalLength);
			buf.erase(buf.begin(), buf.begin() + pGamePacket->TotalLength);
		}

		if (!discardedBytes.empty()) {
			std::string buffer = std::format("Discarded Bytes ({}b)\n\t", discardedBytes.size());
			char map[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
			for (size_t i = 0; i < discardedBytes.size(); ++i) {
				const auto b = discardedBytes[i];
				buffer.push_back(map[b >> 4]);
				buffer.push_back(map[b & 0b1111]);
				buffer.push_back(' ');
				if (i % 32 == 31 && i != discardedBytes.size() - 1)
					buffer.append("\n\t");
				if (i % 4 == 3 && i != discardedBytes.size() - 1)
					buffer.push_back(' ');
			}
			logger->Log(App::LogCategory::SocketHook, buffer, App::LogLevel::Warning);
		}
	}
};

class App::Network::SingleConnection::Implementation {
	friend class SocketHook;
public:
	std::shared_ptr<Misc::Logger> const m_logger;
	SingleConnection* const this_;
	SocketHook* const hook_;
	const SOCKET m_socket;
	bool m_unloading = false;

	std::map<size_t, std::vector<std::function<bool(Structures::FFXIVBundle*, Structures::FFXIVMessage*, std::vector<uint8_t>&)>>> m_incomingHandlers;
	std::map<size_t, std::vector<std::function<bool(Structures::FFXIVBundle*, Structures::FFXIVMessage*, std::vector<uint8_t>&)>>> m_outgoingHandlers;

	std::deque<uint64_t> m_keepAliveRequestTimestamps;
	std::deque<uint64_t> m_observedServerResponseList;
	std::deque<int64_t> m_observedConnectionLatencyList;

	SingleStream m_recvRaw;
	SingleStream m_recvProcessed;
	SingleStream m_sendRaw;
	SingleStream m_sendProcessed;

	sockaddr_storage m_localAddress = { AF_UNSPEC };
	sockaddr_storage m_remoteAddress = { AF_UNSPEC };

	Utils::CallOnDestruction m_pingTrackKeeper;

	mutable int m_nIoctlTcpInfoFailureCount = 0;

	uint64_t m_nextTcpDelaySetAttempt = 0;

	Implementation(SingleConnection* this_, SocketHook* hook_, SOCKET s)
		: m_logger(App::Misc::Logger::Acquire())
		, this_(this_)
		, hook_(hook_)
		, m_socket(s) {

		m_logger->Format(LogCategory::SocketHook, "{:x}: Found", m_socket);
		ResolveAddresses();
	}

	void SetTCPDelay() {
		if (m_nextTcpDelaySetAttempt > GetTickCount64())
			return;

		DWORD buf = 0, cb = 0;

		// SIO_TCP_SET_ACK_FREQUENCY: Controls ACK delay every x ACK. Default delay is 40 or 200ms. Default value is 2 ACKs, set to 1 to disable ACK delay.
		// SIO_TCP_SET_ACK_FREQUENCY: Controls ACK delay every x ACK. Default delay is 40 or 200ms. Default value is 2 ACKs, set to 1 to disable ACK delay.
		int freq = 1;
		if (SOCKET_ERROR == WSAIoctl(m_socket, SIO_TCP_SET_ACK_FREQUENCY, &freq, sizeof freq, &buf, sizeof buf, &cb, nullptr, nullptr)) {
			m_nextTcpDelaySetAttempt = GetTickCount64() + 1000;
			throw Utils::Win32::Error(WSAGetLastError(), "WSAIoctl(SIO_TCP_SET_ACK_FREQUENCY, 1, 0)");
		}

		// TCP_NODELAY: if enabled, sends packets as soon as possible instead of waiting for ACK or large packet.
		int optval = 1;
		if (SOCKET_ERROR == setsockopt(m_socket, SOL_SOCKET, TCP_NODELAY, reinterpret_cast<char*>(&optval), sizeof optval)) {
			m_nextTcpDelaySetAttempt = GetTickCount64() + 1000;
			throw Utils::Win32::Error(WSAGetLastError(), "setsockopt(TCP_NODELAY, 1)");
		}

		m_nextTcpDelaySetAttempt = GetTickCount64();
	}

	void ResolveAddresses();

	void Unload() {
		m_recvRaw.m_ending = true;
		m_sendRaw.m_ending = true;
		m_recvProcessed.m_ending = true;
		m_sendProcessed.m_ending = true;
		m_unloading = true;
	}

	void AttemptReceive() {
		uint8_t buf[4096];
		unsigned long readable;
		while (true) {
			readable = 0;
			if (ioctlsocket(m_socket, FIONREAD, &readable) == SOCKET_ERROR)
				break;
			if (!readable)
				break;
			const auto recvlen = hook_->recv.bridge(m_socket, reinterpret_cast<char*>(buf), sizeof buf, 0);
			if (recvlen > 0)
				m_recvRaw.Write(buf, recvlen);
		}
		ProcessRecvData();
	}

	void AttemptSend() {
		uint8_t buf[4096];
		while (m_sendProcessed.Available()) {
			const auto len = m_sendProcessed.Peek(buf, sizeof buf);
			const auto sent = hook_->send.bridge(m_socket, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
			if (sent == SOCKET_ERROR)
				break;
			m_sendProcessed.Consume(sent);
		}
	}

	void ProcessRecvData() {
		using namespace Structures;
		SingleStream::ProxyAvailableData(m_recvRaw, m_recvProcessed, [&](const FFXIVBundle* pGamePacket, SingleStream& target) {
			if (!pGamePacket->MessageCount) {
				target.Write(pGamePacket, pGamePacket->TotalLength);
				return;
			}

			std::vector<std::vector<uint8_t>> messages;
			try {
				messages = pGamePacket->GetMessages();
			} catch (std::exception& e) {
				pGamePacket->DebugPrint(LogCategory::SocketHook, e.what());
				target.Write(pGamePacket, pGamePacket->TotalLength);
				return;
			}

			std::vector<uint8_t> data;
			data.reserve(65536);
			data.insert(data.end(),
				reinterpret_cast<const uint8_t*>(pGamePacket),
				reinterpret_cast<const uint8_t*>(pGamePacket) + GamePacketHeaderSize);
			auto* pHeader = reinterpret_cast<FFXIVBundle*>(&data[0]);
			pHeader->MessageCount = 0;

			for (auto& message : messages) {
				bool use = true;
				const auto pMessage = reinterpret_cast<FFXIVMessage*>(&message[0]);

				if (pMessage->Type == SegmentType::ServerKeepAlive) {
					if (!m_keepAliveRequestTimestamps.empty()) {
						uint64_t delay;
						do {
							delay = Utils::GetHighPerformanceCounter() - m_keepAliveRequestTimestamps.front();
							m_keepAliveRequestTimestamps.pop_front();
						} while (!m_keepAliveRequestTimestamps.empty() && delay > 5000);

						// Add statistics sample
						this_->ApplicationLatency.AddValue(delay);
						if (const auto latency = this_->FetchSocketLatency())
							this_->SocketLatency.AddValue(latency);
					}
				} else if (pMessage->Type == SegmentType::IPC) {
					for (const auto& cbs : m_incomingHandlers) {
						for (const auto& cb : cbs.second) {
							std::vector<uint8_t> buf;
							use &= cb(pHeader, pMessage, buf);
							if (!buf.empty())
								data.insert(data.end(), buf.begin(), buf.end());
						}
					}
				}

				if (use) {
					data.insert(data.end(), message.begin(), message.begin() + pMessage->Length);
					pHeader->MessageCount++;
				}
			}
			if (!pHeader->MessageCount)
				return;

			pHeader->TotalLength = static_cast<uint16_t>(data.size());
			pHeader->GzipCompressed = 0;
			target.Write(&data[0], data.size());
			}, m_logger.get());
	}

	void ProcessSendData() {
		using namespace Structures;
		SingleStream::ProxyAvailableData(m_sendRaw, m_sendProcessed, [&](const FFXIVBundle* pGamePacket, SingleStream& target) {
			if (!pGamePacket->MessageCount) {
				target.Write(pGamePacket, pGamePacket->TotalLength);
				return;
			}

			std::vector<std::vector<uint8_t>> messages;
			try {
				messages = pGamePacket->GetMessages();
			} catch (std::exception&) {
				target.Write(pGamePacket, pGamePacket->TotalLength);
				return;
			}

			std::vector<uint8_t> data;
			data.reserve(65536);
			data.insert(data.end(),
				reinterpret_cast<const uint8_t*>(pGamePacket),
				reinterpret_cast<const uint8_t*>(pGamePacket) + GamePacketHeaderSize);
			auto* pHeader = reinterpret_cast<FFXIVBundle*>(&data[0]);
			pHeader->MessageCount = 0;

			for (auto& message : messages) {
				bool use = true;
				const auto pMessage = reinterpret_cast<FFXIVMessage*>(&message[0]);

				if (pMessage->Type == SegmentType::ClientKeepAlive) {
					m_keepAliveRequestTimestamps.push_back(Utils::GetHighPerformanceCounter());
				} else if (pMessage->Type == SegmentType::IPC) {
					for (const auto& cbs : m_outgoingHandlers) {
						for (const auto& cb : cbs.second) {
							std::vector<uint8_t> buf;
							use &= cb(pHeader, pMessage, buf);
							if (!buf.empty())
								data.insert(data.end(), buf.begin(), buf.end());
						}
					}
				}

				if (use) {
					data.insert(data.end(), message.begin(), message.end());
					pHeader->MessageCount++;
				}
			}
			if (!pHeader->MessageCount)
				return;

			pHeader->GzipCompressed = 0;
			pHeader->TotalLength = static_cast<uint16_t>(data.size());
			target.Write(&data[0], data.size());
			}, m_logger.get());
	}

	bool CloseRecvIfPossible() {
		if (!m_recvRaw.Available() && !m_recvProcessed.Available() && m_unloading)
			m_recvRaw.m_closed = m_recvProcessed.m_closed = true;
		return IsFinished();
	}

	bool CloseSendIfPossible() {
		if (!m_sendRaw.Available() && !m_sendProcessed.Available() && m_unloading)
			m_sendRaw.m_closed = m_sendProcessed.m_closed = true;
		return IsFinished();
	}

	bool IsFinished() const {
		return m_recvRaw.m_closed && m_sendRaw.m_closed;
	}
};

class App::Network::SocketHook::Implementation {
public:
	const std::shared_ptr<Config> m_config;
	SocketHook* const this_;
	XivAlexApp* const m_pApp;
	DWORD const m_dwGameMainThreadId;
	IcmpPingTracker m_pingTracker;
	std::map<SOCKET, std::unique_ptr<SingleConnection>> m_sockets;
	std::set<SOCKET> m_nonGameSockets;
	bool m_unloading = false;
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedIpRange;
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedPortRange;
	Utils::CallOnDestruction::Multiple m_cleanupList;

	Implementation(SocketHook* this_, XivAlexApp* pApp)
		: m_config(Config::Acquire())
		, this_(this_)
		, m_pApp(pApp)
		, m_dwGameMainThreadId(GetWindowThreadProcessId(pApp->GetGameWindowHandle(), nullptr)) {
		auto reparse = [this](Config::ItemBase&) {
			ParseTakeOverAddresses();
			this->this_->ReleaseSockets();
		};
		m_cleanupList += m_config->Game.Server_IpRange.OnChangeListener(reparse);
		m_cleanupList += m_config->Game.Server_PortRange.OnChangeListener(reparse);
		m_cleanupList += m_config->Runtime.TakeOverAllAddresses.OnChangeListener(reparse);
		m_cleanupList += m_config->Runtime.TakeOverPrivateAddresses.OnChangeListener(reparse);
		m_cleanupList += m_config->Runtime.TakeOverLoopbackAddresses.OnChangeListener(reparse);
		m_cleanupList += m_config->Runtime.TakeOverAllPorts.OnChangeListener(reparse);
		ParseTakeOverAddresses();
	}

	~Implementation() = default;

	void ParseTakeOverAddresses() {
		const auto& game = m_config->Game;
		const auto& runtime = m_config->Runtime;
		try {
			m_allowedIpRange = Utils::ParseIpRange(game.Server_IpRange, runtime.TakeOverAllAddresses, runtime.TakeOverPrivateAddresses, runtime.TakeOverLoopbackAddresses);
		} catch (std::exception& e) {
			this_->m_logger->Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
		}
		try {
			m_allowedPortRange = Utils::ParsePortRange(game.Server_PortRange, runtime.TakeOverAllPorts);
		} catch (std::exception& e) {
			this_->m_logger->Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
		}
	}

	enum class TestRemoteAddressResult {
		Pass = 0,
		RegisterIgnore = 1,
		TakeOver = 2,
	};

	TestRemoteAddressResult TestRemoteAddressAndLog(SOCKET s) {
		sockaddr_in addr{};
		int namelen = sizeof addr;
		if (0 != getpeername(s, reinterpret_cast<sockaddr*>(&addr), &namelen))
			return TestRemoteAddressResult::Pass; // Not interested if not connected yet

		if (addr.sin_family != AF_INET) {
			this_->m_logger->Format(LogCategory::SocketHook, "{:x}: Mark ignored; not IPv4", s);
			m_nonGameSockets.emplace(s);
			return TestRemoteAddressResult::RegisterIgnore;
		}

		if (!m_allowedIpRange.empty()) {
			const uint32_t ip = ntohl(addr.sin_addr.s_addr);
			bool pass = false;
			for (const auto& range : m_allowedIpRange) {
				if (range.first <= ip && ip <= range.second) {
					pass = true;
					break;
				}
			}
			if (!pass) {
				this_->m_logger->Format(LogCategory::SocketHook, "{:x}: Mark ignored; remote={}; IP address not accepted", s, Utils::ToString(addr));
				return TestRemoteAddressResult::RegisterIgnore;
			}
		}
		if (!m_allowedPortRange.empty()) {
			bool pass = false;
			for (const auto& range : m_allowedPortRange) {
				if (range.first <= addr.sin_port && addr.sin_port <= range.second) {
					pass = true;
					break;
				}
			}
			if (!pass) {
				this_->m_logger->Format(LogCategory::SocketHook, "{:x}: Mark ignored; remote={}; port not accepted", s, Utils::ToString(addr));
				return TestRemoteAddressResult::RegisterIgnore;
			}
		}
		return TestRemoteAddressResult::TakeOver;
	}

	SingleConnection* FindOrCreateSingleConnection(SOCKET s, bool existingOnly = false) {
		if (const auto found = m_sockets.find(s); found != m_sockets.end()) {
			found->second->ResolveAddresses();
			return found->second.get();
		}
		if (m_unloading
			|| existingOnly
			|| m_nonGameSockets.find(s) != m_nonGameSockets.end())
			return nullptr;

		switch (TestRemoteAddressAndLog(s)) {
			case TestRemoteAddressResult::Pass:
				return nullptr;
			case TestRemoteAddressResult::RegisterIgnore:
				m_nonGameSockets.emplace(s);
				return nullptr;
			case TestRemoteAddressResult::TakeOver:
				break;
		}
		m_sockets.emplace(s, std::make_unique<SingleConnection>(this_, s));
		const auto ptr = m_sockets.at(s).get();
		this_->OnSocketFound(*ptr);
		return ptr;
	}

	decltype(m_sockets.end()) CleanupSocket(decltype(m_sockets.end()) it) {
		if (it == m_sockets.end())
			return it;
		this_->OnSocketGone(*it->second);
		return m_sockets.erase(it);
	}

	decltype(m_sockets.end()) CleanupSocket(SOCKET s) {
		return CleanupSocket(m_sockets.find(s));
	}
};

void App::Network::SingleConnection::Implementation::ResolveAddresses() {
	sockaddr_storage local{}, remote{};
	socklen_t addrlen = sizeof local;
	if (0 == getsockname(m_socket, reinterpret_cast<sockaddr*>(&local), &addrlen) && Utils::CompareSockaddr(&m_localAddress, &local)) {
		m_localAddress = local;
		m_logger->Format(LogCategory::SocketHook, "{:x}: Local={}", m_socket, Utils::ToString(local));

		// Set TCP delay here because SIO_TCP_SET_ACK_FREQUENCY seems to work only when localAddress is not 0.0.0.0.
		if (hook_->m_pImpl->m_config->Runtime.ReducePacketDelay && reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr != INADDR_ANY) {
			SetTCPDelay();
		}
	}
	addrlen = sizeof remote;
	if (0 == getpeername(m_socket, reinterpret_cast<sockaddr*>(&remote), &addrlen) && Utils::CompareSockaddr(&m_remoteAddress, &remote)) {
		m_remoteAddress = remote;
		m_logger->Format(LogCategory::SocketHook, "{:x}: Remote={}", m_socket, Utils::ToString(remote));
	}

	const auto& local4 = *reinterpret_cast<const sockaddr_in*>(&m_localAddress);
	const auto& remote4 = *reinterpret_cast<const sockaddr_in*>(&m_remoteAddress);
	if (local4.sin_family == AF_INET && remote4.sin_family == AF_INET && local4.sin_addr.s_addr && remote4.sin_addr.s_addr && !m_pingTrackKeeper)
		m_pingTrackKeeper = hook_->m_pImpl->m_pingTracker.Track(
			reinterpret_cast<sockaddr_in*>(&m_localAddress)->sin_addr,
			reinterpret_cast<sockaddr_in*>(&m_remoteAddress)->sin_addr
		);
}

App::Network::SingleConnection::SingleConnection(SocketHook* hook, SOCKET s)
	: m_pImpl(std::make_unique<Implementation>(this, hook, s)) {

}
App::Network::SingleConnection::~SingleConnection() = default;

void App::Network::SingleConnection::AddIncomingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVBundle*, Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb) {
	this->m_pImpl->m_incomingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void App::Network::SingleConnection::AddOutgoingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVBundle*, Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb) {
	this->m_pImpl->m_outgoingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void App::Network::SingleConnection::RemoveMessageHandlers(void* token) {
	this->m_pImpl->m_incomingHandlers.erase(reinterpret_cast<size_t>(token));
	this->m_pImpl->m_outgoingHandlers.erase(reinterpret_cast<size_t>(token));
}

SOCKET App::Network::SingleConnection::GetSocket() const {
	return m_pImpl->m_socket;
}

void App::Network::SingleConnection::ResolveAddresses() {
	m_pImpl->ResolveAddresses();
}

int64_t App::Network::SingleConnection::FetchSocketLatency() {
	if (m_pImpl->m_nIoctlTcpInfoFailureCount >= 5)
		return INT64_MAX;

	TCP_INFO_v0 info{};
	DWORD tcpInfoVersion = 0, cb = 0;
	if (0 != WSAIoctl(m_pImpl->m_socket, SIO_TCP_INFO, &tcpInfoVersion, sizeof tcpInfoVersion, &info, sizeof info, &cb, nullptr, nullptr)) {
		m_pImpl->m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0 failed: {:08x}", m_pImpl->m_socket, WSAGetLastError());
		m_pImpl->m_nIoctlTcpInfoFailureCount++;
		return INT64_MAX;
	} else if (cb != sizeof info) {
		m_pImpl->m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0: buffer size mismatch ({} != {})", m_pImpl->m_socket, cb, sizeof info);
		m_pImpl->m_nIoctlTcpInfoFailureCount++;
		return INT64_MAX;
	} else {
		const auto latency = info.RttUs / 1000LL;
		SocketLatency.AddValue(latency);
		return latency;
	}
};

const Utils::NumericStatisticsTracker* App::Network::SingleConnection::GetPingLatencyTracker() const {
	if (m_pImpl->m_localAddress.ss_family != AF_INET || m_pImpl->m_remoteAddress.ss_family != AF_INET)
		return nullptr;
	const auto &local = *reinterpret_cast<const sockaddr_in*>(&m_pImpl->m_localAddress);
	const auto &remote = *reinterpret_cast<const sockaddr_in*>(&m_pImpl->m_remoteAddress);
	if (!local.sin_addr.s_addr || !remote.sin_addr.s_addr)
		return nullptr;
	return m_pImpl->hook_->m_pImpl->m_pingTracker.GetTracker(local.sin_addr, remote.sin_addr);
}

App::Network::SocketHook::SocketHook(XivAlexApp* pApp)
	: m_logger(Misc::Logger::Acquire())
	, OnSocketFound([this](const auto& cb) {
		for (const auto& item : this->m_pImpl->m_sockets)
			cb(*item.second);
	})
	, m_pImpl(std::make_unique<Implementation>(this, pApp)) {

	pApp->RunOnGameLoop([&]() {
		m_pImpl->m_cleanupList += std::move(socket.SetHook([&](_In_ int af, _In_ int type, _In_ int protocol) {
			const auto result = socket.bridge(af, type, protocol);
			if (GetCurrentThreadId() == m_pImpl->m_dwGameMainThreadId) {
				m_logger->Format(LogCategory::SocketHook, "{:x}: New", result);
				m_pImpl->FindOrCreateSingleConnection(result);
			}
			return result;
			}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

		m_pImpl->m_cleanupList += std::move(closesocket.SetHook([&](SOCKET s) {
			if (GetCurrentThreadId() != m_pImpl->m_dwGameMainThreadId)
				return closesocket.bridge(s);

			m_pImpl->CleanupSocket(s);
			m_logger->Format(LogCategory::SocketHook, "{:x}: Close", s);
			m_pImpl->m_nonGameSockets.erase(s);
			return closesocket.bridge(s);
			}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

		m_pImpl->m_cleanupList += std::move(send.SetHook([&](SOCKET s, const char* buf, int len, int flags) {
			if (GetCurrentThreadId() != m_pImpl->m_dwGameMainThreadId)
				return send.bridge(s, buf, len, flags);

			const auto conn = m_pImpl->FindOrCreateSingleConnection(s);
			if (conn == nullptr)
				return send.bridge(s, buf, len, flags);

			conn->m_pImpl->m_sendRaw.Write(buf, len);
			conn->m_pImpl->ProcessSendData();
			conn->m_pImpl->AttemptSend();
			return len;
			}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

		m_pImpl->m_cleanupList += std::move(recv.SetHook([&](SOCKET s, char* buf, int len, int flags) {
			if (GetCurrentThreadId() != m_pImpl->m_dwGameMainThreadId)
				return recv.bridge(s, buf, len, flags);

			const auto conn = m_pImpl->FindOrCreateSingleConnection(s);
			if (conn == nullptr)
				return recv.bridge(s, buf, len, flags);

			conn->m_pImpl->AttemptReceive();

			const auto result = conn->m_pImpl->m_recvProcessed.Read(reinterpret_cast<uint8_t*>(buf), len);
			if (conn->m_pImpl->CloseRecvIfPossible())
				m_pImpl->CleanupSocket(s);

			return static_cast<int>(result);
			}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

		m_pImpl->m_cleanupList += std::move(select.SetHook([&](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) {
			if (GetCurrentThreadId() != m_pImpl->m_dwGameMainThreadId)
				return select.bridge(nfds, readfds, writefds, exceptfds, timeout);

			const fd_set readfds_original = *readfds;
			fd_set readfds_temp = *readfds;

			select.bridge(nfds, &readfds_temp, writefds, exceptfds, timeout);

			FD_ZERO(readfds);
			for (size_t i = 0; i < readfds_original.fd_count; ++i) {
				const auto s = readfds_original.fd_array[i];
				const auto conn = m_pImpl->FindOrCreateSingleConnection(s);
				if (conn == nullptr) {
					if (FD_ISSET(s, &readfds_temp))
						FD_SET(s, readfds);
					continue;
				}

				if (FD_ISSET(s, &readfds_temp))
					conn->m_pImpl->AttemptReceive();

				if (conn->m_pImpl->m_recvProcessed.Available())
					FD_SET(s, readfds);

				if (conn->m_pImpl->CloseRecvIfPossible())
					m_pImpl->CleanupSocket(s);
			}

			for (auto it = m_pImpl->m_sockets.begin(); it != m_pImpl->m_sockets.end(); ) {
				const auto& [s, conn] = *it;

				conn->m_pImpl->AttemptSend();

				if (conn->m_pImpl->CloseSendIfPossible())
					it = m_pImpl->CleanupSocket(s);
				else
					++it;
			}

			return static_cast<int>((readfds ? readfds->fd_count : 0) +
				(writefds ? writefds->fd_count : 0) +
				(exceptfds ? exceptfds->fd_count : 0));
			}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

		m_pImpl->m_cleanupList += std::move(connect.SetHook([&](SOCKET s, const sockaddr* name, int namelen) {
			if (GetCurrentThreadId() != m_pImpl->m_dwGameMainThreadId)
				return connect.bridge(s, name, namelen);

			const auto result = connect.bridge(s, name, namelen);
			m_logger->Format(LogCategory::SocketHook, "{:x}: Connect: {}", s, Utils::ToString(*name));
			return result;
			}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

		});
}

App::Network::SocketHook::~SocketHook() {
	m_pImpl->m_unloading = true;
	while (!m_pImpl->m_sockets.empty()) {
		m_pImpl->m_pApp->RunOnGameLoop([this]() { this->ReleaseSockets(); });
		Sleep(1);
	}

	// Let it process main message loop first to ensure that no socket operation is in progress
	SendMessageW(m_pImpl->m_pApp->GetGameWindowHandle(), WM_NULL, 0, 0);
	m_pImpl->m_cleanupList.Clear();
}

bool App::Network::SocketHook::IsUnloadable() const {
	return socket.IsDisableable()
		&& connect.IsDisableable()
		&& select.IsDisableable()
		&& recv.IsDisableable()
		&& send.IsDisableable()
		&& closesocket.IsDisableable();
}

void App::Network::SocketHook::ReleaseSockets() {
	for (auto& [s, con] : m_pImpl->m_sockets) {
		if (con->m_pImpl->m_unloading)
			continue;
		
		m_logger->Format(LogCategory::SocketHook, "{:x}: Detaching", s);
		con->m_pImpl->Unload();
	}
	m_pImpl->m_nonGameSockets.clear();
}

std::wstring App::Network::SocketHook::Describe() const {
	while (true) {
		try {
			std::wstring result;
			for (const auto& [s, conn] : m_pImpl->m_sockets) {
				result += std::format(L"Connection {:x} ({} -> {})\n",
					s,
					Utils::FromUtf8(Utils::ToString(conn->m_pImpl->m_localAddress)),
					Utils::FromUtf8(Utils::ToString(conn->m_pImpl->m_remoteAddress)));
				
				if (const auto latency = conn->FetchSocketLatency()) {
					result += std::format(L"* Socket Latency: last {}ms, med {}ms, avg {}+{}ms\n",
						latency, conn->SocketLatency.Median(), conn->SocketLatency.Mean(), conn->SocketLatency.Deviation());
				} else
					result += L"* Socket Latency: failed to resolve\n";
				
				if (const auto tracker = conn->GetPingLatencyTracker(); tracker && tracker->Count()) {
					result += std::format(L"* Ping Latency: last {}ms, med {}ms, avg {}+{}ms\n",
						tracker->Latest(), tracker->Median(), tracker->Mean(), tracker->Deviation());
				} else
					result += L"* Ping Latency: failed to resolve\n";
				
				result += std::format(L"* Response Delay: med {}ms, avg {}ms, dev {}ms\n\n",
					conn->ApplicationLatency.Median(), conn->ApplicationLatency.Mean(), conn->ApplicationLatency.Deviation());
			}
			return result;
		} catch (...) {
			// ignore
		}
	}
}
