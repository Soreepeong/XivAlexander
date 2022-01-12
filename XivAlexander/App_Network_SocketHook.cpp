#include "pch.h"
#include "App_Network_SocketHook.h"

#include <XivAlexanderCommon/XaZlib.h>

#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"
#include "App_Network_IcmpPingTracker.h"
#include "App_Network_Structures.h"
#include "App_XivAlexApp.h"
#include "resource.h"

static constexpr auto SecondToMicrosecondMultiplier = 1000000ULL;

class SingleStream {
public:
	bool m_ending = false;
	bool m_closed = false;

	Utils::ZlibReusableInflater m_inflater;

	std::vector<uint8_t> m_pending{};
	std::vector<size_t> m_reservedOffset{};
	size_t m_pendingStartPos = 0;

	class SingleStreamWriter {
		SingleStream& m_stream;
		const size_t m_offset;
		size_t m_commitLength = 0;

	public:
		SingleStreamWriter(SingleStream& stream)
			: m_stream(stream)
			, m_offset(stream.m_pending.size()) {
		}

		template<typename T>
		T* Allocate(size_t length) {
			m_stream.m_pending.resize(m_offset + m_commitLength + length);
			return reinterpret_cast<T*>(&m_stream.m_pending[m_offset + m_commitLength]);
		}

		size_t Write(size_t length) {
			m_commitLength += length;
			return length;
		}

		~SingleStreamWriter() {  // NOLINT(bugprone-exception-escape)
			m_stream.m_pending.resize(m_offset + m_commitLength);
		}
	};

	SingleStreamWriter Write() {
		return {*this};
	}

	void Write(const void* buf, size_t length) {
		const auto uint8buf = static_cast<const uint8_t*>(buf);
		m_pending.insert(m_pending.end(), uint8buf, uint8buf + length);
	}

	template<typename T, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	void Write(const T& data) {
		Write(&data, sizeof data);
	}

	template<typename T = uint8_t, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	void Write(const std::span<T>& data) {
		Write(data.data(), data.size_bytes());
	}

	template<typename T = uint8_t, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	[[nodiscard]] std::span<const T> Peek(size_t count = SIZE_MAX) const {
		if (m_pending.empty())
			return {};
		return {
			reinterpret_cast<const T*>(&m_pending[m_pendingStartPos]),
			count == SIZE_MAX ? (m_pending.size() - m_pendingStartPos) / sizeof T : count
		};
	}

	template<typename T = uint8_t, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	void Consume(size_t count) {
		m_pendingStartPos += count * sizeof T;
		if (m_pendingStartPos == m_pending.size()) {
			m_pending.clear();
			m_pendingStartPos = 0;
		} else if (m_pendingStartPos > m_pending.size()) {
			__debugbreak();
		}
	}

	template<typename T, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	size_t Read(T* buf, size_t count) {
		count = std::min(count, (m_pending.size() - m_pendingStartPos) / sizeof T);
		memcpy(buf, &m_pending[m_pendingStartPos], count * sizeof T);
		Consume<T>(count);
		return count;
	}

	template<typename T = uint8_t, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	[[nodiscard]] size_t Available() const {
		return (m_pending.size() - m_pendingStartPos) / sizeof T;
	}

	void TunnelXivStream(SingleStream& target, const App::Network::SingleConnection::MessageMangler& messageMangler) {
		using namespace App::Network::Structures;
		while (true) {
			auto buf = Peek();
			if (buf.empty())
				break;

			if (const auto trash = XivBundle::ExtractFrontTrash(buf); !trash.empty()) {
				target.Write(trash);
				Consume(trash.size_bytes());
				buf = buf.subspan(trash.size_bytes());
			}

			// Incomplete header
			if (buf.size_bytes() < sizeof XivBundleHeader)
				break;

			const auto* pGamePacket = reinterpret_cast<const XivBundle*>(buf.data());

			// Invalid TotalLength
			if (pGamePacket->TotalLength == 0) {
				target.Write(buf.subspan(0, 1));
				Consume(1);
				continue;
			}

			// Incomplete data
			if (buf.size_bytes() < pGamePacket->TotalLength)
				break;

			try {
				auto messages = pGamePacket->GetMessages(m_inflater);
				auto header = *pGamePacket;
				header.TotalLength = static_cast<uint32_t>(sizeof XivBundleHeader);
				header.MessageCount = 0;
				header.GzipCompressed = 0;

				for (auto& message : messages) {
					const auto pMessage = reinterpret_cast<XivMessage*>(&message[0]);

					if (!messageMangler(pMessage))
						pMessage->Length = 0;

					if (!pMessage->Length)
						continue;

					header.TotalLength += pMessage->Length;
					header.MessageCount += 1;
				}

				target.Write(&header, sizeof XivBundleHeader);
				for (const auto& message : messages) {
					if (reinterpret_cast<const XivMessage*>(&message[0])->Length)
						target.Write(std::span(message));
				}
			} catch (const std::exception& e) {
				pGamePacket->DebugPrint(App::LogCategory::SocketHook, e.what());
				target.Write(pGamePacket, pGamePacket->TotalLength);
			}

			Consume(pGamePacket->TotalLength);
		}
	}
};

struct App::Network::SingleConnection::Implementation {
	std::shared_ptr<Misc::Logger> const m_logger;
	std::shared_ptr<Config> const m_config;
	SingleConnection* const this_;
	SocketHook* const hook_;
	bool m_unloading = false;

	std::map<size_t, std::vector<MessageMangler>> m_incomingHandlers{};
	std::map<size_t, std::vector<MessageMangler>> m_outgoingHandlers{};

	std::deque<uint64_t> m_keepAliveRequestTimestampsUs{};
	std::deque<uint64_t> m_observedServerResponseList{};
	std::deque<int64_t> m_observedConnectionLatencyList{};

	SingleStream m_recvRaw;
	SingleStream m_recvProcessed;
	SingleStream m_sendRaw;
	SingleStream m_sendProcessed;

	sockaddr_storage m_localAddress = {AF_UNSPEC};
	sockaddr_storage m_remoteAddress = {AF_UNSPEC};

	Utils::CallOnDestruction m_pingTrackKeeper;

	mutable int m_nIoctlTcpInfoFailureCount = 0;

	uint64_t m_nextTcpDelaySetAttempt = 0;

	Implementation(SingleConnection* this_, SocketHook* hook_)
		: m_logger(Misc::Logger::Acquire())
		, m_config(Config::Acquire())
		, this_(this_)
		, hook_(hook_) {

		m_logger->Format(LogCategory::SocketHook, m_config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_FOUND, this_->m_socket);
		ResolveAddresses();
	}

	void SetTCPDelay() {
		if (m_nextTcpDelaySetAttempt > GetTickCount64())
			return;

		DWORD buf = 0, cb = 0;

		// SIO_TCP_SET_ACK_FREQUENCY: Controls ACK delay every x ACK. Default delay is 40 or 200ms. Default value is 2 ACKs, set to 1 to disable ACK delay.
		int freq = 1;
		if (SOCKET_ERROR == WSAIoctl(this_->m_socket, SIO_TCP_SET_ACK_FREQUENCY, &freq, sizeof freq, &buf, sizeof buf, &cb, nullptr, nullptr)) {
			m_nextTcpDelaySetAttempt = GetTickCount64() + 1000;
			throw Utils::Win32::Error(WSAGetLastError(), "WSAIoctl(SIO_TCP_SET_ACK_FREQUENCY, 1, 0)");
		}

		// TCP_NODELAY: if enabled, sends packets as soon as possible instead of waiting for ACK or large packet.
		int optval = 1;
		if (SOCKET_ERROR == setsockopt(this_->m_socket, SOL_SOCKET, TCP_NODELAY, reinterpret_cast<char*>(&optval), sizeof optval)) {
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
		if (auto write = m_recvRaw.Write();
			!write.Write(std::max(0, hook_->recv.bridge(this_->m_socket, write.Allocate<char>(65536), 65536, 0))))
			return;

		ProcessRecvData();
	}

	void AttemptSend() {
		const auto data = m_sendProcessed.Peek<char>();
		if (data.empty())
			return;

		const auto sent = hook_->send.bridge(this_->m_socket, data.data(), static_cast<int>(data.size_bytes()), 0);
		if (sent == SOCKET_ERROR)
			return;

		m_sendProcessed.Consume(sent);
	}

	void ProcessRecvData() {
		m_recvRaw.TunnelXivStream(m_recvProcessed, [&](auto* pMessage) {
			auto use = true;

			switch (pMessage->Type) {
				case Structures::MessageType::ServerKeepAlive:
					if (!m_keepAliveRequestTimestampsUs.empty()) {
						int64_t delayUs;
						do {
							delayUs = static_cast<int64_t>(Utils::QpcUs() - m_keepAliveRequestTimestampsUs.front());
							m_keepAliveRequestTimestampsUs.pop_front();
						} while (!m_keepAliveRequestTimestampsUs.empty() && delayUs > 5000000);

						// Add statistics sample
						this_->ApplicationLatencyUs.AddValue(delayUs);
						if (const auto latency = this_->FetchSocketLatencyUs())
							this_->SocketLatencyUs.AddValue(latency);
					}
					break;

				case Structures::MessageType::Ipc:
					for (const auto& cbs : m_incomingHandlers) {
						for (const auto& cb : cbs.second) {
							use &= cb(pMessage);
						}
					}
			}

			return use;
		});
	}

	void ProcessSendData() {
		m_sendRaw.TunnelXivStream(m_sendProcessed, [&](auto* pMessage) {
			auto use = true;

			switch (pMessage->Type) {
				case Structures::MessageType::ClientKeepAlive:
					m_keepAliveRequestTimestampsUs.push_back(Utils::QpcUs());
					break;

				case Structures::MessageType::Ipc:
					for (const auto& cbs : m_outgoingHandlers) {
						for (const auto& cb : cbs.second) {
							use &= cb(pMessage);
						}
					}
			}

			return use;
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

struct App::Network::SocketHook::Implementation {
	const std::shared_ptr<Config> m_config;
	SocketHook* const this_;
	XivAlexApp* const m_pApp;
	DWORD const m_dwGameMainThreadId;
	IcmpPingTracker m_pingTracker;
	std::map<SOCKET, std::unique_ptr<SingleConnection>> m_sockets{};
	std::set<SOCKET> m_nonGameSockets{};
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedIpRange{};
	std::vector<std::pair<uint32_t, uint32_t>> m_allowedPortRange{};
	Utils::CallOnDestruction::Multiple m_cleanupList;
	int64_t LastSocketSelectCounterUs{};

	Implementation(SocketHook* this_, XivAlexApp* pApp)
		: m_config(Config::Acquire())
		, this_(this_)
		, m_pApp(pApp)
		, m_dwGameMainThreadId(pApp->GetGameWindowThreadId()) {
		auto reparse = [this]() {
			ParseTakeOverAddresses();
			this->this_->ReleaseSockets();
		};
		m_cleanupList += m_config->Game.Server_IpRange.OnChange(reparse);
		m_cleanupList += m_config->Game.Server_PortRange.OnChange(reparse);
		m_cleanupList += m_config->Runtime.TakeOverAllAddresses.OnChange(reparse);
		m_cleanupList += m_config->Runtime.TakeOverPrivateAddresses.OnChange(reparse);
		m_cleanupList += m_config->Runtime.TakeOverLoopbackAddresses.OnChange(reparse);
		m_cleanupList += m_config->Runtime.TakeOverAllPorts.OnChange(reparse);
		ParseTakeOverAddresses();
	}

	~Implementation() = default;

	void ParseTakeOverAddresses() {
		const auto& game = m_config->Game;
		const auto& runtime = m_config->Runtime;
		try {
			m_allowedIpRange = Utils::ParseIpRange(game.Server_IpRange, runtime.TakeOverAllAddresses, runtime.TakeOverPrivateAddresses, runtime.TakeOverLoopbackAddresses);
		} catch (const std::exception& e) {
			this_->m_logger->Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
		}
		try {
			m_allowedPortRange = Utils::ParsePortRange(game.Server_PortRange, runtime.TakeOverAllPorts);
		} catch (const std::exception& e) {
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
			return TestRemoteAddressResult::Pass;  // Not interested if not connected yet

		if (addr.sin_family != AF_INET) {
			this_->m_logger->Format(LogCategory::SocketHook, m_config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_IGNORED_NOT_IPV4, s);
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
				this_->m_logger->Format(LogCategory::SocketHook, m_config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_IGNORED_IP, s, Utils::ToString(addr));
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
				this_->m_logger->Format(LogCategory::SocketHook, m_config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_IGNORED_PORT, s, Utils::ToString(addr));
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
		if (this_->m_unloading
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
	if (0 == getsockname(this_->m_socket, reinterpret_cast<sockaddr*>(&local), &addrlen) && Utils::CompareSockaddr(&m_localAddress, &local)) {
		m_localAddress = local;
		m_logger->Format(LogCategory::SocketHook, "{:x}: Local={}", this_->m_socket, Utils::ToString(local));

		// Set TCP delay here because SIO_TCP_SET_ACK_FREQUENCY seems to work only when localAddress is not 0.0.0.0.
		if (hook_->m_pImpl->m_config->Runtime.ReducePacketDelay && reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr != INADDR_ANY) {
			try {
				SetTCPDelay();
			} catch (const Utils::Win32::Error&) {
				// don't print error
			}
		}
	}
	addrlen = sizeof remote;
	if (0 == getpeername(this_->m_socket, reinterpret_cast<sockaddr*>(&remote), &addrlen) && Utils::CompareSockaddr(&m_remoteAddress, &remote)) {
		m_remoteAddress = remote;
		m_logger->Format(LogCategory::SocketHook, "{:x}: Remote={}", this_->m_socket, Utils::ToString(remote));
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
	: m_socket(s)
	, m_pImpl(std::make_unique<Implementation>(this, hook)) {

}

App::Network::SingleConnection::~SingleConnection() = default;

void App::Network::SingleConnection::AddIncomingFFXIVMessageHandler(void* token, MessageMangler cb) {
	this->m_pImpl->m_incomingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void App::Network::SingleConnection::AddOutgoingFFXIVMessageHandler(void* token, MessageMangler cb) {
	this->m_pImpl->m_outgoingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void App::Network::SingleConnection::RemoveMessageHandlers(void* token) {
	this->m_pImpl->m_incomingHandlers.erase(reinterpret_cast<size_t>(token));
	this->m_pImpl->m_outgoingHandlers.erase(reinterpret_cast<size_t>(token));
}

void App::Network::SingleConnection::ResolveAddresses() {
	m_pImpl->ResolveAddresses();
}

int64_t App::Network::SingleConnection::FetchSocketLatencyUs() {
	if (m_pImpl->m_nIoctlTcpInfoFailureCount >= 5)
		return INT64_MAX;

	TCP_INFO_v0 info{};
	DWORD tcpInfoVersion = 0, cb = 0;
	if (0 != WSAIoctl(m_socket, SIO_TCP_INFO, &tcpInfoVersion, sizeof tcpInfoVersion, &info, sizeof info, &cb, nullptr, nullptr)) {
		const auto err = WSAGetLastError();
		m_pImpl->m_logger->Format<LogLevel::Info>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0: {:08x} ({})", m_socket,
			err, Utils::Win32::FormatWindowsErrorMessage(err));
		m_pImpl->m_nIoctlTcpInfoFailureCount++;
		return INT64_MAX;
	} else if (cb != sizeof info) {
		m_pImpl->m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0: buffer size mismatch ({} != {})", m_socket, cb, sizeof info);
		m_pImpl->m_nIoctlTcpInfoFailureCount++;
		return INT64_MAX;
	} else {
		const auto latency = info.RttUs;
		SocketLatencyUs.AddValue(latency);
		return latency;
	}
};

const Utils::NumericStatisticsTracker* App::Network::SingleConnection::GetPingLatencyTrackerUs() const {
	if (m_pImpl->m_localAddress.ss_family != AF_INET || m_pImpl->m_remoteAddress.ss_family != AF_INET)
		return nullptr;
	const auto& local = *reinterpret_cast<const sockaddr_in*>(&m_pImpl->m_localAddress);
	const auto& remote = *reinterpret_cast<const sockaddr_in*>(&m_pImpl->m_remoteAddress);
	if (!local.sin_addr.s_addr || !remote.sin_addr.s_addr)
		return nullptr;
	return m_pImpl->hook_->m_pImpl->m_pingTracker.GetTrackerUs(local.sin_addr, remote.sin_addr);
}

App::Network::SocketHook::SocketHook(XivAlexApp* pApp)
	: m_logger(Misc::Logger::Acquire())
	, OnSocketFound([this](const auto& cb) {
		if (m_pImpl) {
			for (const auto& val : this->m_pImpl->m_sockets | std::views::values)
				cb(*val);
		}
	}) {

	m_hThreadSetupHook = Utils::Win32::Thread(L"SocketHook::SocketHook", [this, pApp]() {
		m_logger->Log(LogCategory::SocketHook, "Waiting for game window to stabilize before setting up redirecting network operations.");
		pApp->RunOnGameLoop([&]() {
			m_pImpl = std::make_unique<Implementation>(this, pApp);
			if (m_unloading)
				return;

			void(Utils::Win32::Thread(L"SocketHook::SocketHook", [this, pApp]() {
				// TODO
				// MessageBoxW(nullptr, L"Test: continuing will load XivAlexander's networking features.", L"XivAlexander DEBUG", MB_OK);

				pApp->RunOnGameLoop([&]() {
					try {
						m_pImpl->m_cleanupList += std::move(socket.SetHook([&](_In_ int af, _In_ int type, _In_ int protocol) {
							const auto result = socket.bridge(af, type, protocol);
							if (GetCurrentThreadId() == m_pImpl->m_dwGameMainThreadId) {
								m_logger->Format(LogCategory::SocketHook, "{:x}: API(socket)", result);
								m_pImpl->FindOrCreateSingleConnection(result);
							}
							return result;
						}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));

						m_pImpl->m_cleanupList += std::move(closesocket.SetHook([&](SOCKET s) {
							if (GetCurrentThreadId() != m_pImpl->m_dwGameMainThreadId)
								return closesocket.bridge(s);

							m_pImpl->CleanupSocket(s);
							m_logger->Format(LogCategory::SocketHook, "{:x}: API(closesocket)", s);
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

							m_pImpl->LastSocketSelectCounterUs = Utils::QpcUs();
							
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

							for (auto it = m_pImpl->m_sockets.begin(); it != m_pImpl->m_sockets.end();) {
								it->second->m_pImpl->AttemptSend();

								if (it->second->m_pImpl->CloseSendIfPossible())
									it = m_pImpl->CleanupSocket(it->first);
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
							m_logger->Format(LogCategory::SocketHook, "{:x}: API(connect): {}", s, Utils::ToString(*name));
							return result;
						}).Wrap([pApp](auto fn) { pApp->RunOnGameLoop(std::move(fn)); }));
						m_logger->Log(LogCategory::SocketHook, "Network operation has been redirected.");

					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "Failed to redirect network operation: {}", e.what());
					}
				});
			}));
		});
	});
}

App::Network::SocketHook::~SocketHook() {
	m_unloading = true;
	m_hThreadSetupHook.Wait();

	if (!m_pImpl)
		return;

	while (!m_pImpl->m_sockets.empty()) {
		m_pImpl->m_pApp->RunOnGameLoop([this]() { this->ReleaseSockets(); });
		Sleep(1);
	}

	if (const auto hGameWnd = m_pImpl->m_pApp->GetGameWindowHandle(false)) {
		// Let it process main message loop first to ensure that no socket operation is in progress
		SendMessageW(hGameWnd, WM_NULL, 0, 0);
	}
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

int64_t App::Network::SocketHook::GetLastSocketSelectCounterUs() const {
	return m_pImpl ? m_pImpl->LastSocketSelectCounterUs : 0;
}

void App::Network::SocketHook::ReleaseSockets() {
	if (!m_pImpl)
		return;

	for (const auto& entry : m_pImpl->m_sockets) {
		if (entry.second->m_pImpl->m_unloading)
			continue;

		m_logger->Format(LogCategory::SocketHook, m_pImpl->m_config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_DETACH, entry.first);
		entry.second->m_pImpl->Unload();
	}
	m_pImpl->m_nonGameSockets.clear();
}

std::wstring App::Network::SocketHook::Describe() const {
	if (!m_pImpl)
		return {};

	while (true) {
		try {
			std::wstring result;
			for (const auto& entry : m_pImpl->m_sockets) {
				const auto& conn = entry.second;
				result += m_pImpl->m_config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_TITLE,
					entry.first,
					Utils::FromUtf8(Utils::ToString(conn->m_pImpl->m_localAddress)),
					Utils::FromUtf8(Utils::ToString(conn->m_pImpl->m_remoteAddress)));

				if (const auto latency = conn->FetchSocketLatencyUs()) {
					const auto [mean, dev] = conn->SocketLatencyUs.MeanAndDeviation();
					result += m_pImpl->m_config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_SOCKET_LATENCY,
						latency, conn->SocketLatencyUs.Median(), mean, dev);
				} else
					result += m_pImpl->m_config->Runtime.GetStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_SOCKET_LATENCY_FAILURE);

				if (const auto tracker = conn->GetPingLatencyTrackerUs(); tracker && tracker->Count()) {
					const auto [mean, dev] = tracker->MeanAndDeviation();
					result += m_pImpl->m_config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_PING_LATENCY,
						tracker->Latest(), tracker->Median(), mean, dev);
				} else
					result += m_pImpl->m_config->Runtime.GetStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_PING_LATENCY_FAILURE);
				
				{
					const auto [mean, dev] = conn->ApplicationLatencyUs.MeanAndDeviation();
					result += m_pImpl->m_config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_RESPONSE_DELAY,
						conn->ApplicationLatencyUs.Median(), mean, dev);
				}
			}
			return result;
		} catch (...) {
			// ignore
		}
	}
}
