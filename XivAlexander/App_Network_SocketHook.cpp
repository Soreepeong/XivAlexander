#include "pch.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"
#include <myzlib.h>
#include <mstcpip.h>

namespace SocketFn = App::Hooks::Socket;

static std::string get_ip_str(const struct sockaddr* sa) {
	char s[1024] = { 0 };
	size_t maxlen = sizeof s;
	switch (sa->sa_family) {
		case AF_INET:
		{
			const auto addr = (struct sockaddr_in*)sa;
			inet_ntop(AF_INET, &(addr->sin_addr), s, maxlen);
			return Utils::FormatString("%s:%d", s, addr->sin_port);
		}

		case AF_INET6:
		{
			const auto addr = (struct sockaddr_in6*)sa;
			inet_ntop(AF_INET6, &(addr->sin6_addr), s, maxlen);
			return Utils::FormatString("%s:%d", s, addr->sin6_port);
		}
	}
	return "Unknown AF";
}

class SingleStream {
public:
	bool ending = false;
	bool closed = false;

	std::vector<uint8_t> m_pending;
	size_t m_pendingStartPos = 0;

	void Write(const void* buf, size_t length) {
		const auto uint8buf = reinterpret_cast<const uint8_t*>(buf);
		m_pending.insert(m_pending.end(), uint8buf, uint8buf + length);
	}

	int Peek(uint8_t* buf, size_t maxlen) const {
		int len = static_cast<int>(std::min({
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

	int Read(uint8_t* buf, size_t maxlen) {
		const auto len = Peek(buf, maxlen);
		Consume(len);
		return len;
	}

	size_t Available() {
		return m_pending.size() - m_pendingStartPos;
	}
};

static
std::vector<std::vector<uint8_t>> GetMessages(const App::Network::Structures::FFXIVBundle* pGamePacket) {
	using namespace App::Network::Structures;

	std::vector<uint8_t> inflatedBuffer;
	std::vector<std::vector<uint8_t>> result;
	const uint8_t* pPointer = reinterpret_cast<const uint8_t*>(pGamePacket) + GamePacketHeaderSize;
	const uint8_t* pUpperBound = pPointer + pGamePacket->TotalLength - GamePacketHeaderSize;

	if (pGamePacket->GzipCompressed) {
		inflatedBuffer = Utils::ZlibDecompress(pPointer, pUpperBound - pPointer);
		if (inflatedBuffer.empty()) {
			return {};
		}
		pPointer = &inflatedBuffer[0];
		pUpperBound = pPointer + inflatedBuffer.size();
	}
	while (pPointer < pUpperBound) {
		const FFXIVMessage* pMessage = reinterpret_cast<const FFXIVMessage*>(pPointer);
		const auto length = pMessage->Length;
		if (pPointer + length <= pUpperBound) {
			result.push_back(std::vector<uint8_t>{pPointer, pPointer + length});
			pPointer += length;
		}
		else {
			// error, discard
			return {};
		}
	}
	return result;
}

static
void ProcessData(SingleStream& source, SingleStream& target, std::function<void(const App::Network::Structures::FFXIVBundle* packet, SingleStream& target)> processor) {
	using namespace App::Network::Structures;

	const auto availableLength = source.Available();
	if (!availableLength)
		return;

	std::vector<uint8_t> discardedBytes;
	std::vector<uint8_t> buf;
	buf.resize(availableLength);
	source.Peek(&buf[0], buf.size());
	while (!buf.empty()) {
		const auto searchLength = std::min(sizeof FFXIVBundle::Magic, buf.size());
		const auto possibleMagicOffset = std::min(
			std::search(buf.begin(), buf.end(), FFXIVBundle::MagicConstant1.begin(), FFXIVBundle::MagicConstant1.begin() + searchLength) - buf.begin(),
			std::search(buf.begin(), buf.end(), FFXIVBundle::MagicConstant2.begin(), FFXIVBundle::MagicConstant2.begin() + searchLength) - buf.begin()
		);
		if (possibleMagicOffset != 0) {
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
		std::string buffer = Utils::FormatString("Discarded Bytes (%db)\n\t", discardedBytes.size());
		char map[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
		size_t i = 0;
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

class App::Network::SingleConnection::Internals {
	friend class SocketHook;
public:
	const SOCKET m_socket;
	bool m_unloading = false;

	std::map<size_t, std::vector<std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)>>> m_incomingHandlers;
	std::map<size_t, std::vector<std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)>>> m_outgoingHandlers;

	std::deque<uint64_t> KeepAliveRequestTimestamps;
	std::vector<uint64_t> ObservedServerResponseList;
	std::vector<int64_t> ObservedConnectionLatencyList;

	SingleStream recvBefore;
	SingleStream recvProcessed;
	SingleStream sendBefore;
	SingleStream sendProcessed;

	sockaddr_storage localAddress = { AF_UNSPEC };
	sockaddr_storage remoteAddress = { AF_UNSPEC };

	Utils::CallOnDestruction m_pingTrackKeeper;

	mutable int m_nIoctlTcpInfoFailureCount = 0;

	Internals(SOCKET s)
		: m_socket(s) {

		Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Found", m_socket);
		ResolveAddresses();
	}

	void ResolveAddresses() {
		sockaddr_storage local, remote;
		socklen_t addrlen = sizeof local;
		if (0 == getsockname(m_socket, reinterpret_cast<sockaddr*>(&local), &addrlen) && Utils::sockaddr_cmp(&localAddress, &local)) {
			localAddress = local;
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Local=%s", m_socket, get_ip_str(reinterpret_cast<sockaddr*>(&local)).c_str());
		}
		addrlen = sizeof remote;
		if (0 == getpeername(m_socket, reinterpret_cast<sockaddr*>(&remote), &addrlen) && Utils::sockaddr_cmp(&remoteAddress, &remote)) {
			remoteAddress = remote;
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Remote=%s", m_socket, get_ip_str(reinterpret_cast<sockaddr*>(&remote)).c_str());
		}
	}

	void Unload() {
		recvBefore.ending = true;
		sendBefore.ending = true;
		recvProcessed.ending = true;
		sendProcessed.ending = true;
		m_unloading = true;
	}

	int64_t GetConnectionLatency() const {
		if (m_nIoctlTcpInfoFailureCount >= 5)
			return 0;

		TCP_INFO_v0 info;
		DWORD tcpInfoVersion = 0, cb;
		if (0 != WSAIoctl(m_socket, SIO_TCP_INFO, &tcpInfoVersion, sizeof tcpInfoVersion, &info, sizeof info, &cb, nullptr, nullptr)) {
			Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::SocketHook, "%p: WSAIoctl SIO_TCP_INFO v0 failed: %08x", m_socket, WSAGetLastError());
			m_nIoctlTcpInfoFailureCount++;
			return 0;
		}
		else if (cb != sizeof info) {
			Misc::Logger::GetLogger().Format<LogLevel::Warning>(LogCategory::SocketHook, "%p: WSAIoctl SIO_TCP_INFO v0: buffer size mismatch (%d != %d)", m_socket, cb, sizeof info);
			m_nIoctlTcpInfoFailureCount++;
			return 0;
		}
		else {
			return std::max(1LL, info.RttUs / 1000LL);
		}
	}

	void AddConnectionLatencyItem(int64_t latency) {
		const int latencyTrackCount = 10;

		ObservedConnectionLatencyList.push_back(latency);

		if (ObservedConnectionLatencyList.size() > latencyTrackCount)
			ObservedConnectionLatencyList.erase(ObservedConnectionLatencyList.begin());
	}

	int64_t GetMedianConnectionLatency() const {
		if (ObservedConnectionLatencyList.empty())
			return 0;

		size_t count = ObservedConnectionLatencyList.size();
		std::vector<int64_t> SortedConnectionLatencyList(count);
		std::partial_sort_copy(ObservedConnectionLatencyList.begin(), ObservedConnectionLatencyList.end(), SortedConnectionLatencyList.begin(), SortedConnectionLatencyList.end());

		if ((count % 2) == 0)
			// even
			return (SortedConnectionLatencyList[count / 2] + SortedConnectionLatencyList[count / 2 - 1]) / 2;

		// odd
		return SortedConnectionLatencyList[count / 2];
	}

	int64_t GetMeanConnectionLatency() const {
		if (ObservedConnectionLatencyList.empty())
			return 0;

		double mean = std::accumulate(ObservedConnectionLatencyList.begin(), ObservedConnectionLatencyList.end(), 0.0) / ObservedConnectionLatencyList.size();

		return std::llround(mean);
	}

	int64_t GetConnectionLatencyDeviation() const {
		size_t count = ObservedConnectionLatencyList.size();

		if (count < 2)
			return 0;

		double mean = std::accumulate(ObservedConnectionLatencyList.begin(), ObservedConnectionLatencyList.end(), 0.0) / count;

		std::vector<double> diff(count);
		std::transform(ObservedConnectionLatencyList.begin(), ObservedConnectionLatencyList.end(), diff.begin(), [mean](int64_t x) { return double(x) - mean; });
		double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
		double stddev = std::sqrt(sq_sum / count);

		return std::llround(stddev);
	}

	void AddServerResponseDelayItem(uint64_t delay) {
		const int latencyTrackCount = 10;

		ObservedServerResponseList.push_back(delay);

		if (ObservedServerResponseList.size() > latencyTrackCount)
			ObservedServerResponseList.erase(ObservedServerResponseList.begin());
	}

	int64_t GetMeanServerResponseDelay() const {
		if (ObservedServerResponseList.empty())
			return 0;

		double mean = std::accumulate(ObservedServerResponseList.begin(), ObservedServerResponseList.end(), 0.0) / ObservedServerResponseList.size();

		return std::llround(mean);
	}

	int64_t GetMedianServerResponseDelay() const {
		if (ObservedServerResponseList.empty())
			return 0;

		size_t count = ObservedServerResponseList.size();
		std::vector<uint64_t> SortedServerResponseList(count);
		std::partial_sort_copy(ObservedServerResponseList.begin(), ObservedServerResponseList.end(), SortedServerResponseList.begin(), SortedServerResponseList.end());
		
		if ((SortedServerResponseList.size() % 2) == 0)
			// even
			return (SortedServerResponseList[count / 2] + SortedServerResponseList[count / 2 - 1]) / 2;

		// odd
		return SortedServerResponseList[count / 2];
	}

	int64_t GetServerResponseDelayDeviation() const {
		size_t count = ObservedServerResponseList.size();

		if (count < 2)
			return 0;

		double mean = std::accumulate(ObservedServerResponseList.begin(), ObservedServerResponseList.end(), 0.0) / count;

		std::vector<double> diff(count);
		std::transform(ObservedServerResponseList.begin(), ObservedServerResponseList.end(), diff.begin(), [mean](int64_t x) { return double(x) - mean; });
		double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
		double stddev = std::sqrt(sq_sum / count);

		return std::llround(stddev);
	}

	void ProcessRecvData() {
		using namespace App::Network::Structures;
		ProcessData(recvBefore, recvProcessed, [&](const FFXIVBundle* pGamePacket, SingleStream& target) {
			if (!pGamePacket->MessageCount) {
				target.Write(pGamePacket, pGamePacket->TotalLength);
				return;
			}

			std::vector<std::vector<uint8_t>> messages;
			try {
				messages = GetMessages(pGamePacket);
			}
			catch (std::exception&) {
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
				auto pMessage = reinterpret_cast<FFXIVMessage*>(&message[0]);

				if (pMessage->Type == SegmentType::ServerKeepAlive) {
					if (!KeepAliveRequestTimestamps.empty()) {
						uint64_t delay;
						do {
							delay = Utils::GetHighPerformanceCounter() - KeepAliveRequestTimestamps.front();
							KeepAliveRequestTimestamps.pop_front();
						} while (!KeepAliveRequestTimestamps.empty() && delay > 5000);

						// Add statistics sample
						AddServerResponseDelayItem(delay);
						AddConnectionLatencyItem(GetConnectionLatency());
					}
				}
				else if (pMessage->Type == SegmentType::IPC) {
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
		using namespace App::Network::Structures;
		ProcessData(sendBefore, sendProcessed, [&](const FFXIVBundle* pGamePacket, SingleStream& target) {
			if (!pGamePacket->MessageCount) {
				target.Write(pGamePacket, pGamePacket->TotalLength);
				return;
			}

			std::vector<std::vector<uint8_t>> messages;
			try {
				messages = GetMessages(pGamePacket);
			}
			catch (std::exception&) {
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
				auto pMessage = reinterpret_cast<FFXIVMessage*>(&message[0]);

				if (pMessage->Type == SegmentType::ClientKeepAlive) {
					KeepAliveRequestTimestamps.push_back(Utils::GetHighPerformanceCounter());
				}
				else if (pMessage->Type == SegmentType::IPC) {
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
		if (!recvBefore.Available() && !recvProcessed.Available() && m_unloading)
			recvBefore.closed = recvProcessed.closed = true;
		return IsFinished();
	}

	bool CloseSendIfPossible() {
		if (!sendBefore.Available() && !sendProcessed.Available() && m_unloading)
			sendBefore.closed = sendProcessed.closed = true;
		return IsFinished();
	}

	bool IsFinished() const {
		return recvBefore.closed && sendBefore.closed;
	}
};

App::Network::SingleConnection::SingleConnection(SOCKET s)
	: impl(std::make_unique<Internals>(s)) {

}
App::Network::SingleConnection::~SingleConnection() = default;

void App::Network::SingleConnection::AddIncomingFFXIVMessageHandler(void* token, std::function<bool(App::Network::Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb) {
	this->impl->m_incomingHandlers[reinterpret_cast<size_t>(token)].push_back(cb);
}

void App::Network::SingleConnection::AddOutgoingFFXIVMessageHandler(void* token, std::function<bool(App::Network::Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb) {
	this->impl->m_outgoingHandlers[reinterpret_cast<size_t>(token)].push_back(cb);
}

void App::Network::SingleConnection::RemoveMessageHandlers(void* token) {
	this->impl->m_incomingHandlers.erase(reinterpret_cast<size_t>(token));
	this->impl->m_outgoingHandlers.erase(reinterpret_cast<size_t>(token));
}

void App::Network::SingleConnection::SendFFXIVMessage(const App::Network::Structures::FFXIVMessage * pMessage) {
	Structures::FFXIVBundle bundle;
	memset(&bundle, 0, sizeof bundle);
	memcpy(bundle.Magic, Structures::FFXIVBundle::MagicConstant1.data(), sizeof bundle.Magic);
	bundle.Timestamp = Utils::GetEpoch();
	bundle.TotalLength = static_cast<uint16_t>(Structures::GamePacketHeaderSize + pMessage->Length);
	bundle.MessageCount = 1;
	impl->recvProcessed.Write(&bundle, sizeof bundle);
	impl->recvProcessed.Write(pMessage, pMessage->Length);
}

SOCKET App::Network::SingleConnection::GetSocket() const {
	return impl->m_socket;
}

void App::Network::SingleConnection::ResolveAddresses() {
	impl->ResolveAddresses();
}

void App::Network::SingleConnection::AddConnectionLatencyItem(int64_t latency) {
	return impl->AddConnectionLatencyItem(latency);
}

int64_t App::Network::SingleConnection::GetMedianConnectionLatency() const {
	return impl->GetMedianConnectionLatency();
}

int64_t App::Network::SingleConnection::GetMeanConnectionLatency() const {
	return impl->GetMeanConnectionLatency();
}

int64_t App::Network::SingleConnection::GetConnectionLatencyDeviation() const {
	return impl->GetConnectionLatencyDeviation();
}

void App::Network::SingleConnection::AddServerResponseDelayItem(uint64_t delay) {
	return impl->AddServerResponseDelayItem(delay);
}

int64_t App::Network::SingleConnection::GetMeanServerResponseDelay() const {
	return impl->GetMeanServerResponseDelay();
}

int64_t App::Network::SingleConnection::GetMedianServerResponseDelay() const {
	return impl->GetMedianServerResponseDelay();
}

int64_t App::Network::SingleConnection::GetServerResponseDelayDeviation() const {
	return impl->GetServerResponseDelayDeviation();
}

int64_t App::Network::SingleConnection::GetConnectionLatency() const {
	return impl->GetConnectionLatency();
}

class App::Network::SocketHook::Internals {
public:
	HWND m_hGameWnd;
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
		m_cleanupList.push_back(ConfigRepository::Config().GameServerIpRange.OnChangeListener([this](ConfigItemBase&) {
			parseIpRange();
			m_nonGameSockets.clear();
			}));
		m_cleanupList.push_back(ConfigRepository::Config().GameServerPortRange.OnChangeListener([this](ConfigItemBase&) {
			parsePortRange();
			m_nonGameSockets.clear();
			}));
		parseIpRange();
		parsePortRange();
		SocketFn::socket.SetupHook([&](_In_ int af, _In_ int type, _In_ int protocol) {
			const auto result = App::Hooks::Socket::socket.bridge(af, type, protocol);
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

			conn->impl->sendBefore.Write(reinterpret_cast<const uint8_t*>(buf), len);
			conn->impl->ProcessSendData();
			AttemptSend(conn);
			return len;
			});
		SocketFn::recv.SetupHook([&](SOCKET s, char* buf, int len, int flags) {
			const auto conn = OnSocketFound(s);
			if (conn == nullptr)
				return SocketFn::recv.bridge(s, buf, len, flags);

			AttemptReceive(conn);

			const auto result = conn->impl->recvProcessed.Read(reinterpret_cast<uint8_t*>(buf), len);
			if (m_unloading && conn->impl->CloseRecvIfPossible())
				CleanupSocket(s);

			return result;
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
					AttemptReceive(conn);

				if (conn->impl->recvProcessed.Available())
					FD_SET(s, readfds);

				if (conn->impl->CloseRecvIfPossible())
					CleanupSocket(s);
			}

			std::vector<SOCKET> writeSockets;
			{
				std::lock_guard<std::mutex> _guard(m_socketMutex);
				for (const auto& pair : m_sockets)
					writeSockets.push_back(pair.first);
			}
			for (const auto s : writeSockets) {
				const auto conn = OnSocketFound(s);
				if (conn == nullptr) {
					continue;
				}

				AttemptSend(conn);

				if (conn->impl->CloseSendIfPossible())
					CleanupSocket(s);
			}

			return (readfds ? readfds->fd_count : 0) +
				(writefds ? writefds->fd_count : 0) +
				(exceptfds ? exceptfds->fd_count : 0);
			});
		SocketFn::connect.SetupHook([&](SOCKET s, const sockaddr* name, int namelen) {
			const auto result = SocketFn::connect.bridge(s, name, namelen);
			Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: connect: %s", s, get_ip_str(name).c_str());
			return result;
			});
	}

	~Internals() {
		Unload();
	}

	int ipToInt(std::string s) {
		std::vector<uint32_t> parts;
		for (const auto& part : Utils::StringSplit(s, "."))
			parts.push_back(std::stoul(Utils::StringTrim(part)));
		if (parts.size() == 1)
			return parts[0];
		else if (parts.size() == 2)
			return (parts[0] << 24) | parts[1];
		else if (parts.size() == 3)
			return (parts[0] << 24) | (parts[1] << 16) | parts[2];
		else if (parts.size() == 4)
			return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
		else
			throw std::exception();
	}

	void parseIpRange() {
		m_allowedIpRange.clear();
		for (auto range : Utils::StringSplit(ConfigRepository::Config().GameServerIpRange, ",")) {
			size_t pos;
			try {
				range = Utils::StringTrim(range);
				if (range.empty())
					continue;
				uint32_t startIp, endIp;
				if ((pos = range.find('/')) != std::string::npos) {
					int subnet = std::stoi(Utils::StringTrim(range.substr(pos + 1)));
					startIp = endIp = ipToInt(range.substr(0, pos));
					if (subnet == 32) {
						startIp = 0;
						endIp = 0xFFFFFFFFUL;
					}
					else if (subnet > 0) {
						startIp = (startIp & ~((1 << (32 - subnet)) - 1));
						endIp = (((endIp >> (32 - subnet)) + 1) << (32 - subnet)) - 1;
					}
				}
				else {
					auto ips = Utils::StringSplit(range, "-");
					if (ips.size() > 2)
						throw std::exception();
					startIp = ipToInt(ips[0]);
					endIp = ips.size() == 2 ? ipToInt(ips[1]) : startIp;
					if (startIp > endIp) {
						const auto t = startIp;
						startIp = endIp;
						endIp = t;
					}
				}
				m_allowedIpRange.emplace_back(startIp, endIp);
			}
			catch (std::exception& e) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::SocketHook, "Invalid IP range item \"%s\": %s. It must be in the form of \"0.0.0.0\", \"0.0.0.0-255.255.255.255\", or \"127.0.0.0/8\", delimited by comma(,).", range.c_str(), e.what());
			}
		}
	}

	void parsePortRange() {
		m_allowedPortRange.clear();
		for (auto range : Utils::StringSplit(ConfigRepository::Config().GameServerPortRange, ",")) {
			try {
				range = Utils::StringTrim(range);
				if (range.empty())
					continue;
				auto ports = Utils::StringSplit(range, "-");
				if (ports.size() > 2)
					throw std::exception();
				uint32_t start = ipToInt(ports[0]);
				uint32_t end = ports.size() == 2 ? ipToInt(ports[1]) : start;
				if (start > end) {
					const auto t = start;
					start = end;
					end = t;
				}
				m_allowedPortRange.emplace_back(start, end);
			}
			catch (std::exception& e) {
				Misc::Logger::GetLogger().Format<LogLevel::Error>(LogCategory::SocketHook, "Invalid port range item \"%s\": %s. It must be in the form of \"0-65535\" or single item, delimited by comma(,).", range.c_str(), e.what());
			}
		}
	}

	bool TestRemoteAddress(const sockaddr_in& addr) {
		if (!m_allowedIpRange.empty()) {
			const uint32_t ip = ((static_cast<uint32_t>(addr.sin_addr.S_un.S_un_b.s_b1) << 24)
				| (static_cast<uint32_t>(addr.sin_addr.S_un.S_un_b.s_b2) << 16)
				| (static_cast<uint32_t>(addr.sin_addr.S_un.S_un_b.s_b3) << 8)
				| (static_cast<uint32_t>(addr.sin_addr.S_un.S_un_b.s_b4) << 0));
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

	void AttemptReceive(SingleConnection* conn) {
		const auto s = conn->GetSocket();
		uint8_t buf[4096];
		unsigned long readable;
		while (true) {
			readable = 0;
			if (ioctlsocket(s, FIONREAD, &readable) == SOCKET_ERROR)
				break;
			if (!readable)
				break;
			int recvlen = SocketFn::recv.bridge(s, reinterpret_cast<char*>(buf), sizeof buf, 0);
			if (recvlen > 0)
				conn->impl->recvBefore.Write(buf, recvlen);
		}
		conn->impl->ProcessRecvData();
	}

	void AttemptSend(SingleConnection* conn) {
		const auto s = conn->GetSocket();
		uint8_t buf[4096];
		while (conn->impl->sendProcessed.Available()) {
			const auto len = conn->impl->sendProcessed.Peek(buf, sizeof buf);
			const auto sent = SocketFn::send.bridge(s, reinterpret_cast<char*>(buf), len, 0);
			if (sent == SOCKET_ERROR)
				break;
			conn->impl->sendProcessed.Consume(sent);
		}
	}

	void Unload() {
		m_unloading = true;
		while (true) {
			{
				std::lock_guard<std::mutex> _guard(m_socketMutex);
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
		std::lock_guard<std::mutex> _guard(m_socketMutex);

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

			sockaddr_in addr_v4;
			int namelen = sizeof addr_v4;
			if (0 != getpeername(socket, reinterpret_cast<sockaddr*>(&addr_v4), &namelen))
				return nullptr; // Not interested if not connected yet

			if (addr_v4.sin_family != AF_INET) {
				m_nonGameSockets.emplace(socket);
				return nullptr;
			}
			if (!TestRemoteAddress(addr_v4)) {
				Misc::Logger::GetLogger().Format(LogCategory::SocketHook, "%p: Mark ignored; remote=%s:%d", socket, get_ip_str(reinterpret_cast<sockaddr*>(&addr_v4)).c_str(), addr_v4.sin_port);
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
		std::lock_guard<std::mutex> _guard(m_socketMutex);
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

void App::Network::SocketHook::AddOnSocketFoundListener(void* token, std::function<void(SingleConnection&)> cb) {
	std::lock_guard<std::mutex> _guard(this->impl->m_socketMutex);
	this->impl->m_onSocketFoundListeners[reinterpret_cast<size_t>(token)].push_back(cb);
	for (const auto& item : this->impl->m_sockets) {
		cb(*item.second);
	}
}

void App::Network::SocketHook::AddOnSocketGoneListener(void* token, std::function<void(SingleConnection&)> cb) {
	std::lock_guard<std::mutex> _guard(this->impl->m_socketMutex);
	this->impl->m_onSocketGoneListeners[reinterpret_cast<size_t>(token)].push_back(cb);
}

void App::Network::SocketHook::RemoveListeners(void* token) {
	std::lock_guard<std::mutex> _guard(this->impl->m_socketMutex);
	this->impl->m_onSocketFoundListeners.erase(reinterpret_cast<size_t>(token));
	this->impl->m_onSocketGoneListeners.erase(reinterpret_cast<size_t>(token));
}
