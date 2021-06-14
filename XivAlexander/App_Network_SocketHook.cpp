#include "pch.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

namespace SocketFn = App::Hooks::Socket;

class SingleStream {
public:
	bool m_ending = false;
	bool m_closed = false;

	std::vector<uint8_t> m_pending;
	size_t m_pendingStartPos = 0;

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

	static void ProxyAvailableData(SingleStream& source, SingleStream& target, const std::function<void(const App::Network::Structures::FFXIVBundle* packet, SingleStream& target)>& processor) {
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
			std::string buffer = Utils::FormatString("Discarded Bytes (%zub)\n\t", discardedBytes.size());
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
			App::Misc::Logger::GetLogger().Log(App::LogCategory::SocketHook, buffer, App::LogLevel::Warning);
		}
	}
};

class App::Network::SingleConnection::Internals {
	friend class SocketHook;
public:
	SingleConnection* const this_;
	const SOCKET m_socket;
	bool m_unloading = false;

	std::map<size_t, std::vector<std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)>>> m_incomingHandlers;
	std::map<size_t, std::vector<std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)>>> m_outgoingHandlers;

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

	Internals(SingleConnection* this_, SOCKET s)
		: this_(this_)
		, m_socket(s) {

		Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Found", m_socket);
		ResolveAddresses();
	}

	void SetTCPDelay() {
		if (m_nextTcpDelaySetAttempt > GetTickCount64())
			return;
		
		DWORD buf = 0, cb = 0;
		
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

	void ResolveAddresses() {
		sockaddr_storage local{}, remote{};
		socklen_t addrlen = sizeof local;
		if (0 == getsockname(m_socket, reinterpret_cast<sockaddr*>(&local), &addrlen) && Utils::CompareSockaddr(&m_localAddress, &local)) {
			m_localAddress = local;
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Local=%s", m_socket, Utils::ToString(local).c_str());

			// Set TCP delay here because SIO_TCP_SET_ACK_FREQUENCY seems to work only when localAddress is not 0.0.0.0.
			if (Config::Instance().Runtime.ReducePacketDelay && reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr != INADDR_ANY) {
				SetTCPDelay();
			}
		}
		addrlen = sizeof remote;
		if (0 == getpeername(m_socket, reinterpret_cast<sockaddr*>(&remote), &addrlen) && Utils::CompareSockaddr(&m_remoteAddress, &remote)) {
			m_remoteAddress = remote;
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Remote=%s", m_socket, Utils::ToString(remote).c_str());
		}
	}

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
			const auto recvlen = SocketFn::recv.bridge(m_socket, reinterpret_cast<char*>(buf), sizeof buf, 0);
			if (recvlen > 0)
				m_recvRaw.Write(buf, recvlen);
		}
		ProcessRecvData();
	}

	void AttemptSend() {
		uint8_t buf[4096];
		while (m_sendProcessed.Available()) {
			const auto len = m_sendProcessed.Peek(buf, sizeof buf);
			const auto sent = SocketFn::send.bridge(m_socket, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
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
						if (int64_t latency; this_->GetCurrentNetworkLatency(latency))
							this_->NetworkLatency.AddValue(latency);
					}
				} else if (pMessage->Type == SegmentType::IPC) {
					for (const auto& cbs : m_incomingHandlers) {
						for (const auto& cb : cbs.second) {
							std::vector<uint8_t> buf;
							use &= cb(pMessage, buf);
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
			});
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
							use &= cb(pMessage, buf);
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
			});
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

App::Network::SingleConnection::SingleConnection(SOCKET s)
	: impl(std::make_unique<Internals>(this, s)) {

}
App::Network::SingleConnection::~SingleConnection() = default;

void App::Network::SingleConnection::AddIncomingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb) {
	this->impl->m_incomingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void App::Network::SingleConnection::AddOutgoingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb) {
	this->impl->m_outgoingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void App::Network::SingleConnection::RemoveMessageHandlers(void* token) {
	this->impl->m_incomingHandlers.erase(reinterpret_cast<size_t>(token));
	this->impl->m_outgoingHandlers.erase(reinterpret_cast<size_t>(token));
}

SOCKET App::Network::SingleConnection::GetSocket() const {
	return impl->m_socket;
}

void App::Network::SingleConnection::ResolveAddresses() {
	impl->ResolveAddresses();
}

bool App::Network::SingleConnection::GetCurrentNetworkLatency(int64_t& latency) const {
	if (impl->m_nIoctlTcpInfoFailureCount >= 5)
		return false;

	TCP_INFO_v0 info{};
	DWORD tcpInfoVersion = 0, cb = 0;
	if (0 != WSAIoctl(impl->m_socket, SIO_TCP_INFO, &tcpInfoVersion, sizeof tcpInfoVersion, &info, sizeof info, &cb, nullptr, nullptr)) {
		Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::SocketHook, "%p: WSAIoctl SIO_TCP_INFO v0 failed: %08x", impl->m_socket, WSAGetLastError());
		impl->m_nIoctlTcpInfoFailureCount++;
		return false;
	} else if (cb != sizeof info) {
		Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::SocketHook, "%p: WSAIoctl SIO_TCP_INFO v0: buffer size mismatch (%d != %d)", impl->m_socket, cb, sizeof info);
		impl->m_nIoctlTcpInfoFailureCount++;
		return false;
	} else {
		latency = info.RttUs / 1000LL;
		return true;
	}
}

class App::Network::SocketHook::Internals {
public:
	HWND m_hGameWnd = nullptr;
	std::map<SOCKET, std::unique_ptr<SingleConnection>> m_sockets;
	std::set<SOCKET> m_nonGameSockets;
	std::map<size_t, std::vector<std::function<void(SingleConnection&)>>> m_onSocketFoundListeners;
	std::map<size_t, std::vector<std::function<void(SingleConnection&)>>> m_onSocketGoneListeners;
	std::mutex m_socketMutex;
	bool m_unloading = false;
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedIpRange;
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedPortRange;
	std::vector<Utils::CallOnDestruction> m_cleanupList;

	Internals() {
		m_cleanupList.push_back(Config::Instance().Game.Server_IpRange.OnChangeListener([this](Config::ItemBase&) {
			parseIpRange();
			m_nonGameSockets.clear();
			}));
		m_cleanupList.push_back(Config::Instance().Game.Server_PortRange.OnChangeListener([this](Config::ItemBase&) {
			parsePortRange();
			m_nonGameSockets.clear();
			}));
		parseIpRange();
		parsePortRange();
		SocketFn::socket.SetupHook([&](_In_ int af, _In_ int type, _In_ int protocol) {
			const auto result = Hooks::Socket::socket.bridge(af, type, protocol);
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: New", result);
			OnSocketFound(result);
			return result;
			});
		SocketFn::closesocket.SetupHook([&](SOCKET s) {
			CleanupSocket(s);
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Close", s);
			m_nonGameSockets.erase(s);
			return SocketFn::closesocket.bridge(s);
			});
		SocketFn::send.SetupHook([&](SOCKET s, const char* buf, int len, int flags) {
			const auto conn = OnSocketFound(s);
			if (conn == nullptr)
				return SocketFn::send.bridge(s, buf, len, flags);

			conn->impl->m_sendRaw.Write(buf, len);
			conn->impl->ProcessSendData();
			conn->impl->AttemptSend();
			return len;
			});
		SocketFn::recv.SetupHook([&](SOCKET s, char* buf, int len, int flags) {
			const auto conn = OnSocketFound(s);
			if (conn == nullptr)
				return SocketFn::recv.bridge(s, buf, len, flags);

			conn->impl->AttemptReceive();

			const auto result = conn->impl->m_recvProcessed.Read(reinterpret_cast<uint8_t*>(buf), len);
			if (m_unloading && conn->impl->CloseRecvIfPossible())
				CleanupSocket(s);

			return static_cast<int>(result);
			});
		SocketFn::select.SetupHook([&](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) {
			const fd_set readfds_original = *readfds;
			fd_set readfds_temp = *readfds;

			SocketFn::select.bridge(nfds, &readfds_temp, writefds, exceptfds, timeout);

			FD_ZERO(readfds);
			for (size_t i = 0; i < readfds_original.fd_count; ++i) {
				const auto s = readfds_original.fd_array[i];
				const auto conn = OnSocketFound(s);
				if (conn == nullptr) {
					if (FD_ISSET(s, &readfds_temp))
						FD_SET(s, readfds);
					continue;
				}

				if (FD_ISSET(s, &readfds_temp))
					conn->impl->AttemptReceive();

				if (conn->impl->m_recvProcessed.Available())
					FD_SET(s, readfds);

				if (conn->impl->CloseRecvIfPossible())
					CleanupSocket(s);
			}

			std::vector<SOCKET> writeSockets;
			{
				std::lock_guard _guard(m_socketMutex);
				for (const auto& pair : m_sockets)
					writeSockets.push_back(pair.first);
			}
			for (const auto s : writeSockets) {
				const auto conn = OnSocketFound(s);
				if (conn == nullptr) {
					continue;
				}

				conn->impl->AttemptSend();

				if (conn->impl->CloseSendIfPossible())
					CleanupSocket(s);
			}

			return (readfds ? readfds->fd_count : 0) +
				(writefds ? writefds->fd_count : 0) +
				(exceptfds ? exceptfds->fd_count : 0);
			});
		SocketFn::connect.SetupHook([&](SOCKET s, const sockaddr* name, int namelen) {
			const auto result = SocketFn::connect.bridge(s, name, namelen);
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: connect: %s", s, Utils::ToString(*name).c_str());
			return result;
			});
	}

	~Internals() {
		Unload();
	}

	static in_addr parseIp(const std::string& s) {
		in_addr addr{};
		switch (inet_pton(AF_INET, &s[0], &addr)) {
			case 1:
				return addr;
			case 0:
				throw std::runtime_error(Utils::FormatString("\"%s\" is an invalid IP address.", s.c_str()).c_str());
			case -1:
				throw Utils::Win32::Error(WSAGetLastError(), "Failed to parse IP address \"%s\".", s.c_str());
			default:
				mark_unreachable_code();
		}
	}

	static uint16_t parsePort(const std::string& s) {
		size_t i = 0;
		const auto parsed = std::stoul(s, &i);
		if (parsed > UINT16_MAX)
			throw std::out_of_range("Not in uint16 range");
		if (i != s.length())
			throw std::out_of_range("Incomplete conversion");
		return static_cast<uint16_t>(parsed);
	}

	void parseIpRange() {
		m_allowedIpRange.clear();
		for (auto& range : Utils::StringSplit(Config::Instance().Game.Server_IpRange, ",")) {
			try {
				range = Utils::StringTrim(range);
				if (range.empty())
					continue;
				uint32_t startIp, endIp;
				if (size_t pos; (pos = range.find('/')) != std::string::npos) {
					const auto subnet = std::stoi(Utils::StringTrim(range.substr(pos + 1)));
					startIp = endIp = ntohl(parseIp(range.substr(0, pos)).s_addr);
					if (subnet == 0) {
						startIp = 0;
						endIp = 0xFFFFFFFFUL;
					} else if (subnet < 32) {
						startIp = (startIp & ~((1 << (32 - subnet)) - 1));
						endIp = (((endIp >> (32 - subnet)) + 1) << (32 - subnet)) - 1;
					}
				} else {
					auto ips = Utils::StringSplit(range, "-");
					if (ips.size() > 2)
						throw std::exception();
					startIp = ntohl(parseIp(ips[0]).s_addr);
					endIp = ips.size() == 2 ? ntohl(parseIp(ips[1]).s_addr) : startIp;
					if (startIp > endIp) {
						const auto t = startIp;
						startIp = endIp;
						endIp = t;
					}
				}
				m_allowedIpRange.emplace_back(startIp, endIp);
			} catch (std::exception& e) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::SocketHook, "Invalid IP range item \"%s\": %s. It must be in the form of \"0.0.0.0\", \"0.0.0.0-255.255.255.255\", or \"127.0.0.0/8\", delimited by comma(,).", range.c_str(), e.what());
			}
		}
	}

	void parsePortRange() {
		m_allowedPortRange.clear();
		for (auto range : Utils::StringSplit(Config::Instance().Game.Server_PortRange, ",")) {
			try {
				range = Utils::StringTrim(range);
				if (range.empty())
					continue;
				auto ports = Utils::StringSplit(range, "-");
				if (ports.size() > 2)
					throw std::exception();
				uint32_t start = parsePort(ports[0]);
				uint32_t end = ports.size() == 2 ? parsePort(ports[1]) : start;
				if (start > end) {
					const auto t = start;
					start = end;
					end = t;
				}
				m_allowedPortRange.emplace_back(start, end);
			} catch (std::exception& e) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::SocketHook, "Invalid port range item \"%s\": %s. It must be in the form of \"0-65535\" or single item, delimited by comma(,).", range.c_str(), e.what());
			}
		}
	}

	bool TestRemoteAddress(const sockaddr_in& addr) {
		if (!m_allowedIpRange.empty()) {
			const uint32_t ip = ntohl(addr.sin_addr.s_addr);
			bool pass = false;
			for (const auto& range : m_allowedIpRange) {
				if (range.first <= ip && ip <= range.second) {
					pass = true;
					break;
				}
			}
			if (!pass)
				return false;
		}
		if (!m_allowedPortRange.empty()) {
			bool pass = false;
			for (const auto& range : m_allowedPortRange) {
				if (range.first <= addr.sin_port && addr.sin_port <= range.second) {
					pass = true;
					break;
				}
			}
			if (!pass)
				return false;
		}
		return true;
	}

	void Unload() {
		m_unloading = true;
		while (true) {
			{
				std::lock_guard _guard(m_socketMutex);
				if (m_sockets.empty())
					break;
				for (auto& item : m_sockets) {
					item.second->impl->Unload();
				}
			}
			Sleep(1);
		}
		SocketFn::socket.SetupHook(nullptr);
		SocketFn::closesocket.SetupHook(nullptr);
		SocketFn::send.SetupHook(nullptr);
		SocketFn::recv.SetupHook(nullptr);
		SocketFn::select.SetupHook(nullptr);
		SocketFn::connect.SetupHook(nullptr);

		// Let it process main message loop first to ensure that no socket operation is in progress
		SendMessage(m_hGameWnd, WM_NULL, 0, 0);
	}

	SingleConnection* OnSocketFound(SOCKET socket, bool existingOnly = false) {
		std::lock_guard _guard(m_socketMutex);

		{
			const auto found = m_sockets.find(socket);

			if (found != m_sockets.end()) {
				found->second->ResolveAddresses();
				return found->second.get();
			}
		}
		{
			const auto found = m_nonGameSockets.find(socket);
			if (found != m_nonGameSockets.end())
				return nullptr;

			sockaddr_in addr{};
			int namelen = sizeof addr;
			if (0 != getpeername(socket, reinterpret_cast<sockaddr*>(&addr), &namelen))
				return nullptr; // Not interested if not connected yet

			if (addr.sin_family != AF_INET) {
				m_nonGameSockets.emplace(socket);
				return nullptr;
			}
			if (!TestRemoteAddress(addr)) {
				Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Mark ignored; remote=%s", socket, Utils::ToString(addr).c_str());
				m_nonGameSockets.emplace(socket);
				return nullptr;
			}
		}
		if (m_unloading || existingOnly)
			return nullptr;
		auto uptr = std::make_unique<SingleConnection>(socket);
		const auto ptr = uptr.get();
		m_sockets.emplace(socket, std::move(uptr));
		for (const auto& listeners : m_onSocketFoundListeners) {
			for (const auto& cb : listeners.second)
				cb(*ptr);
		}
		return ptr;
	}

	void CleanupSocket(SOCKET socket) {
		std::lock_guard _guard(m_socketMutex);
		const auto f = m_sockets.find(socket);
		if (f == m_sockets.end())
			return;
		for (const auto& listeners : m_onSocketGoneListeners) {
			for (const auto& cb : listeners.second)
				cb(*f->second);
		}
		m_sockets.erase(f);
	}
};

static App::Network::SocketHook* s_socketHookInstance;

App::Network::SocketHook::SocketHook(HWND hGameWnd)
	: impl(std::make_unique<Internals>()) {
	impl->m_hGameWnd = hGameWnd;
	s_socketHookInstance = this;
}

App::Network::SocketHook::~SocketHook() {
	s_socketHookInstance = nullptr;
}

App::Network::SocketHook* App::Network::SocketHook::Instance() {
	return s_socketHookInstance;
}

void App::Network::SocketHook::AddOnSocketFoundListener(void* token, const std::function<void(SingleConnection&)>&cb) {
	std::lock_guard _guard(this->impl->m_socketMutex);
	this->impl->m_onSocketFoundListeners[reinterpret_cast<size_t>(token)].emplace_back(cb);
	for (const auto& item : this->impl->m_sockets) {
		cb(*item.second);
	}
}

void App::Network::SocketHook::AddOnSocketGoneListener(void* token, const std::function<void(SingleConnection&)>&cb) {
	std::lock_guard _guard(this->impl->m_socketMutex);
	this->impl->m_onSocketGoneListeners[reinterpret_cast<size_t>(token)].emplace_back(cb);
}

void App::Network::SocketHook::RemoveListeners(void* token) {
	std::lock_guard _guard(this->impl->m_socketMutex);
	this->impl->m_onSocketFoundListeners.erase(reinterpret_cast<size_t>(token));
	this->impl->m_onSocketGoneListeners.erase(reinterpret_cast<size_t>(token));
}

std::wstring App::Network::SocketHook::Describe() const {
	std::lock_guard lock(impl->m_socketMutex);

	std::wstring result;
	for (const auto& [s, conn] : impl->m_sockets) {
		result += Utils::FormatString(L"Connection %p (%s -> %s)\n",
			s,
			Utils::FromUtf8(Utils::ToString(conn->impl->m_localAddress)).c_str(),
			Utils::FromUtf8(Utils::ToString(conn->impl->m_remoteAddress)).c_str());
		if (int64_t latency; conn->GetCurrentNetworkLatency(latency)) {
			result += Utils::FormatString(L"* Latency: last %lldms, med %lldms, avg %lldms, dev %lldms\n",
				latency, conn->NetworkLatency.Median(), conn->NetworkLatency.Mean(), conn->NetworkLatency.Deviation());
		} else
			result += L"* Latency: failed to resolve\n";
		result += Utils::FormatString(L"* Response Delay: med %lldms, avg %lldms, dev %lldms\n\n",
			conn->ApplicationLatency.Median(), conn->ApplicationLatency.Mean(), conn->ApplicationLatency.Deviation());
	}
	return result;
}
