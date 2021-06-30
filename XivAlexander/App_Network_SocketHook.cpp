#include "pch.h"
#include "App_Network_SocketHook.h"

#include "App_App.h"
#include "App_Network_IcmpPingTracker.h"
#include "App_Network_Structures.h"

namespace App::Network::HookedSocketFunctions_ {
	using namespace Hooks;
	extern ImportedFunction<SOCKET, int, int, int> socket;
	extern ImportedFunction<int, SOCKET, const sockaddr*, int> connect;
	extern ImportedFunction<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select;
	extern ImportedFunction<int, SOCKET, char*, int, int> recv;
	extern ImportedFunction<int, SOCKET, const char*, int, int> send;
	extern ImportedFunction<int, SOCKET> closesocket;

	ImportedFunction<SOCKET, int, int, int> socket("socket::socket", "ws2_32.dll", "socket", 23,
		[](int af, int type, int protocol) { return socket.Thunked(af, type, protocol); });

	ImportedFunction<int, SOCKET, const sockaddr*, int> connect("socket::connect", "ws2_32.dll", "connect", 4,
		[](SOCKET s, const sockaddr* name, int namelen) { return connect.Thunked(s, name, namelen); });

	ImportedFunction<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select("socket::select", "ws2_32.dll", "select", 18,
		[](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) { return select.Thunked(nfds, readfds, writefds, exceptfds, timeout); });

	ImportedFunction<int, SOCKET, char*, int, int> recv("socket::recv", "ws2_32.dll", "recv", 16,
		[](SOCKET s, char* buf, int len, int flags) { return recv.Thunked(s, buf, len, flags); });

	ImportedFunction<int, SOCKET, const char*, int, int> send("socket::send", "ws2_32.dll", "send", 19,
		[](SOCKET s, const char* buf, int len, int flags) { return send.Thunked(s, buf, len, flags); });

	ImportedFunction<int, SOCKET> closesocket("socket::closesocket", "ws2_32.dll", "closesocket", 3,
		[](SOCKET s) { return closesocket.Thunked(s); });
}

namespace SocketFn = App::Network::HookedSocketFunctions_;

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

		Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Found", m_socket);
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

	void ResolveAddresses() {
		sockaddr_storage local{}, remote{};
		socklen_t addrlen = sizeof local;
		if (0 == getsockname(m_socket, reinterpret_cast<sockaddr*>(&local), &addrlen) && Utils::CompareSockaddr(&m_localAddress, &local)) {
			m_localAddress = local;
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Local={}", m_socket, Utils::ToString(local));

			// Set TCP delay here because SIO_TCP_SET_ACK_FREQUENCY seems to work only when localAddress is not 0.0.0.0.
			if (Config::Instance().Runtime.ReducePacketDelay && reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr != INADDR_ANY) {
				SetTCPDelay();
			}
		}
		addrlen = sizeof remote;
		if (0 == getpeername(m_socket, reinterpret_cast<sockaddr*>(&remote), &addrlen) && Utils::CompareSockaddr(&m_remoteAddress, &remote)) {
			m_remoteAddress = remote;
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Remote={}", m_socket, Utils::ToString(remote));
		}

		const auto& local4 = *reinterpret_cast<const sockaddr_in*>(&m_localAddress);
		const auto& remote4 = *reinterpret_cast<const sockaddr_in*>(&m_remoteAddress);
		if (local4.sin_family == AF_INET && remote4.sin_family == AF_INET && local4.sin_addr.s_addr && remote4.sin_addr.s_addr && !m_pingTrackKeeper)
			m_pingTrackKeeper = IcmpPingTracker::GetInstance().Track(
				reinterpret_cast<sockaddr_in*>(&m_localAddress)->sin_addr,
				reinterpret_cast<sockaddr_in*>(&m_remoteAddress)->sin_addr
			);
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
						if (const auto latency = this_->FetchSocketLatency())
							this_->SocketLatency.AddValue(latency);
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

int64_t App::Network::SingleConnection::FetchSocketLatency() {
	if (impl->m_nIoctlTcpInfoFailureCount >= 5)
		return INT64_MAX;

	TCP_INFO_v0 info{};
	DWORD tcpInfoVersion = 0, cb = 0;
	if (0 != WSAIoctl(impl->m_socket, SIO_TCP_INFO, &tcpInfoVersion, sizeof tcpInfoVersion, &info, sizeof info, &cb, nullptr, nullptr)) {
		Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0 failed: {:08x}", impl->m_socket, WSAGetLastError());
		impl->m_nIoctlTcpInfoFailureCount++;
		return INT64_MAX;
	} else if (cb != sizeof info) {
		Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0: buffer size mismatch ({} != {})", impl->m_socket, cb, sizeof info);
		impl->m_nIoctlTcpInfoFailureCount++;
		return INT64_MAX;
	} else {
		const auto latency = info.RttUs / 1000LL;
		SocketLatency.AddValue(latency);
		return latency;
	}
};

class App::Network::SocketHook::Internals {
	SocketHook* this_;

public:
	App* const m_pApp;
	DWORD const m_dwGameMainThreadId;
	std::map<SOCKET, std::unique_ptr<SingleConnection>> m_sockets;
	std::set<SOCKET> m_nonGameSockets;
	std::map<size_t, std::vector<std::function<void(SingleConnection&)>>> m_onSocketFoundListeners;
	std::map<size_t, std::vector<std::function<void(SingleConnection&)>>> m_onSocketGoneListeners;
	bool m_unloading = false;
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedIpRange;
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedPortRange;
	Utils::CallOnDestruction::Multiple m_cleanupList;
	
	Internals(SocketHook* this_, App* pApp)
		: this_(this_)
		, m_pApp(pApp)
		, m_dwGameMainThreadId(GetWindowThreadProcessId(pApp->GetGameWindowHandle(), nullptr)) {
		auto reparse = [this](Config::ItemBase&) {
			ParseTakeOverAddresses();
			this->this_->ReleaseSockets();
		};
		m_cleanupList += Config::Instance().Game.Server_IpRange.OnChangeListener(reparse);
		m_cleanupList += Config::Instance().Game.Server_PortRange.OnChangeListener(reparse);
		m_cleanupList += Config::Instance().Runtime.TakeOverAllAddresses.OnChangeListener(reparse);
		m_cleanupList += Config::Instance().Runtime.TakeOverPrivateAddresses.OnChangeListener(reparse);
		m_cleanupList += Config::Instance().Runtime.TakeOverLoopbackAddresses.OnChangeListener(reparse);
		m_cleanupList += Config::Instance().Runtime.TakeOverAllPorts.OnChangeListener(reparse);
		ParseTakeOverAddresses();

		pApp->QueueRunOnMessageLoop([&]() {
			m_cleanupList += std::move(SocketFn::socket.SetHook([&](_In_ int af, _In_ int type, _In_ int protocol) {
				const auto result = SocketFn::socket.bridge(af, type, protocol);
				if (GetCurrentThreadId() == m_dwGameMainThreadId) {
					Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: New", result);
					FindOrCreateSingleConnection(result);
				}
				return result;
				}).Wrap([pApp](auto fn) { pApp->QueueRunOnMessageLoop(std::move(fn)); }));

			m_cleanupList += std::move(SocketFn::closesocket.SetHook([&](SOCKET s) {
				if (GetCurrentThreadId() != m_dwGameMainThreadId)
					return SocketFn::closesocket.bridge(s);

				CleanupSocket(s);
				Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Close", s);
				m_nonGameSockets.erase(s);
				return SocketFn::closesocket.bridge(s);
				}).Wrap([pApp](auto fn) { pApp->QueueRunOnMessageLoop(std::move(fn)); }));
			
			m_cleanupList += std::move(SocketFn::send.SetHook([&](SOCKET s, const char* buf, int len, int flags) {
				if (GetCurrentThreadId() != m_dwGameMainThreadId)
					return SocketFn::send.bridge(s, buf, len, flags);

				const auto conn = FindOrCreateSingleConnection(s);
				if (conn == nullptr)
					return SocketFn::send.bridge(s, buf, len, flags);

				conn->impl->m_sendRaw.Write(buf, len);
				conn->impl->ProcessSendData();
				conn->impl->AttemptSend();
				return len;
				}).Wrap([pApp](auto fn) { pApp->QueueRunOnMessageLoop(std::move(fn)); }));
			
			m_cleanupList += std::move(SocketFn::recv.SetHook([&](SOCKET s, char* buf, int len, int flags) {
				if (GetCurrentThreadId() != m_dwGameMainThreadId)
					return SocketFn::recv.bridge(s, buf, len, flags);

				const auto conn = FindOrCreateSingleConnection(s);
				if (conn == nullptr)
					return SocketFn::recv.bridge(s, buf, len, flags);

				conn->impl->AttemptReceive();

				const auto result = conn->impl->m_recvProcessed.Read(reinterpret_cast<uint8_t*>(buf), len);
				if (conn->impl->CloseRecvIfPossible())
					CleanupSocket(s);

				return static_cast<int>(result);
				}).Wrap([pApp](auto fn) { pApp->QueueRunOnMessageLoop(std::move(fn)); }));
			
			m_cleanupList += std::move(SocketFn::select.SetHook([&](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) {
				if (GetCurrentThreadId() != m_dwGameMainThreadId)
					return SocketFn::select.bridge(nfds, readfds, writefds, exceptfds, timeout);

				const fd_set readfds_original = *readfds;
				fd_set readfds_temp = *readfds;

				SocketFn::select.bridge(nfds, &readfds_temp, writefds, exceptfds, timeout);

				FD_ZERO(readfds);
				for (size_t i = 0; i < readfds_original.fd_count; ++i) {
					const auto s = readfds_original.fd_array[i];
					const auto conn = FindOrCreateSingleConnection(s);
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

				for (auto it = m_sockets.begin(); it != m_sockets.end(); ) {
					const auto& [s, conn] = *it;

					conn->impl->AttemptSend();

					if (conn->impl->CloseSendIfPossible())
						it = CleanupSocket(s);
					else
						++it;
				}

				return static_cast<int>((readfds ? readfds->fd_count : 0) +
					(writefds ? writefds->fd_count : 0) +
					(exceptfds ? exceptfds->fd_count : 0));
				}).Wrap([pApp](auto fn) { pApp->QueueRunOnMessageLoop(std::move(fn)); }));
			
			m_cleanupList += std::move(SocketFn::connect.SetHook([&](SOCKET s, const sockaddr* name, int namelen) {
				if (GetCurrentThreadId() != m_dwGameMainThreadId)
					return SocketFn::connect.bridge(s, name, namelen);

				const auto result = SocketFn::connect.bridge(s, name, namelen);
				Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Connect: {}", s, Utils::ToString(*name));
				return result;
				}).Wrap([pApp](auto fn) { pApp->QueueRunOnMessageLoop(std::move(fn)); }));
			
		});
	}

	~Internals() {
		m_unloading = true;
		while (!m_sockets.empty()) {
			App::Instance()->QueueRunOnMessageLoop([this]() { this->this_->ReleaseSockets(); });
			Sleep(1);
		}
		
		// Let it process main message loop first to ensure that no socket operation is in progress
		SendMessage(m_pApp->GetGameWindowHandle(), WM_NULL, 0, 0);
	}

	void ParseTakeOverAddresses() {
		const auto& game = Config::Instance().Game;
		const auto& runtime = Config::Instance().Runtime;
		try {
			m_allowedIpRange = Utils::ParseIpRange(game.Server_IpRange, runtime.TakeOverAllAddresses, runtime.TakeOverPrivateAddresses, runtime.TakeOverLoopbackAddresses);
		} catch (std::exception& e) {
			Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
		}
		try {
			m_allowedPortRange = Utils::ParsePortRange(game.Server_PortRange, runtime.TakeOverAllPorts);
		} catch (std::exception& e) {
			Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
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
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Mark ignored; not IPv4", s);
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
				Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Mark ignored; remote={}; IP address not accepted", s, Utils::ToString(addr));
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
				Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Mark ignored; remote={}; port not accepted", s, Utils::ToString(addr));
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
		m_sockets.emplace(s, std::make_unique<SingleConnection>(s));
		const auto ptr = m_sockets.at(s).get();
		for (const auto& listeners : m_onSocketFoundListeners) {
			for (const auto& cb : listeners.second)
				cb(*ptr);
		}
		return ptr;
	}

	decltype(m_sockets.end()) CleanupSocket(decltype(m_sockets.end()) it) {
		if (it == m_sockets.end())
			return it;
		for (const auto& listeners : m_onSocketGoneListeners) {
			for (const auto& cb : listeners.second)
				cb(*it->second);
		}
		return m_sockets.erase(it);
	}

	decltype(m_sockets.end()) CleanupSocket(SOCKET s) {
		return CleanupSocket(m_sockets.find(s));
	}
};

static App::Network::SocketHook* s_socketHookInstance;

const Utils::NumericStatisticsTracker* App::Network::SingleConnection::GetPingLatencyTracker() const {
	if (impl->m_localAddress.ss_family != AF_INET || impl->m_remoteAddress.ss_family != AF_INET)
		return nullptr;
	const auto &local = *reinterpret_cast<const sockaddr_in*>(&impl->m_localAddress);
	const auto &remote = *reinterpret_cast<const sockaddr_in*>(&impl->m_remoteAddress);
	if (!local.sin_addr.s_addr || !remote.sin_addr.s_addr)
		return nullptr;
	return IcmpPingTracker::GetInstance().GetTracker(local.sin_addr, remote.sin_addr);
}

App::Network::SocketHook::SocketHook(App* pApp)
	: impl(std::make_unique<Internals>(this, pApp)) {
	s_socketHookInstance = this;
}

App::Network::SocketHook::~SocketHook() {
	s_socketHookInstance = nullptr;
}

App::Network::SocketHook* App::Network::SocketHook::Instance() {
	return s_socketHookInstance;
}

void App::Network::SocketHook::AddOnSocketFoundListener(void* token, const std::function<void(SingleConnection&)>&cb) {
	App::Instance()->QueueRunOnMessageLoop([this, token, &cb]() {
		this->impl->m_onSocketFoundListeners[reinterpret_cast<size_t>(token)].emplace_back(cb);
		for (const auto& item : this->impl->m_sockets) {
			cb(*item.second);
		}
	});
}

void App::Network::SocketHook::AddOnSocketGoneListener(void* token, const std::function<void(SingleConnection&)>&cb) {
	App::Instance()->QueueRunOnMessageLoop([this, token, &cb]() {
		this->impl->m_onSocketGoneListeners[reinterpret_cast<size_t>(token)].emplace_back(cb);
	});
}

void App::Network::SocketHook::RemoveListeners(void* token) {
	App::Instance()->QueueRunOnMessageLoop([this, token]() {
		this->impl->m_onSocketFoundListeners.erase(reinterpret_cast<size_t>(token));
		this->impl->m_onSocketGoneListeners.erase(reinterpret_cast<size_t>(token));
	});
}

void App::Network::SocketHook::ReleaseSockets() {
	for (auto& [s, con] : impl->m_sockets) {
		if (con->impl->m_unloading)
			continue;
		
		Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "{:x}: Detaching", s);
		con->impl->Unload();
	}
	impl->m_nonGameSockets.clear();
}

std::wstring App::Network::SocketHook::Describe() const {
	while (true) {
		try {
			std::wstring result;
			for (const auto& [s, conn] : impl->m_sockets) {
				result += std::format(L"Connection {:x} ({} -> {})\n",
					s,
					Utils::FromUtf8(Utils::ToString(conn->impl->m_localAddress)),
					Utils::FromUtf8(Utils::ToString(conn->impl->m_remoteAddress)));
				
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
