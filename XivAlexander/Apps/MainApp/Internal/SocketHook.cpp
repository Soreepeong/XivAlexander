#include "pch.h"
#include "SocketHook.h"

#include <XivAlexanderCommon/Sqex/Network/Structure.h>
#include <XivAlexanderCommon/Utils/ZlibWrapper.h>

#include "Apps/MainApp/App.h"
#include "Config.h"
#include "Misc/IcmpPingTracker.h"
#include "Misc/Logger.h"
#include "resource.h"

using namespace Sqex::Network::Structure;

static Utils::OodleNetworkFunctions s_oodle{};

class XivAlexander::Apps::MainApp::Internal::SingleConnection::SingleStream {
	Misc::Logger& m_logger;
	const std::string m_name;
	Utils::ZlibReusableDeflater m_deflater;
	Utils::ZlibReusableInflater m_inflater;
	Utils::Oodler m_oodler, m_unoodler;

	std::vector<uint8_t> m_buffer{};
	size_t m_pointer = 0;

public:
	class SingleStreamWriter {
		SingleStream& m_stream;
		const size_t m_offset;
		size_t m_commitLength = 0;

	public:
		SingleStreamWriter(SingleStream& stream)
			: m_stream(stream)
			, m_offset(stream.m_buffer.size()) {
		}

		template<typename T>
		T* Allocate(size_t length) {
			m_stream.m_buffer.resize(m_offset + m_commitLength + length);
			return reinterpret_cast<T*>(&m_stream.m_buffer[m_offset + m_commitLength]);
		}

		size_t Write(size_t length) {
			m_commitLength += length;
			return length;
		}

		~SingleStreamWriter() {  // NOLINT(bugprone-exception-escape)
			m_stream.m_buffer.resize(m_offset + m_commitLength);
		}
	};

	SingleStream(Misc::Logger& logger, std::string name)
		: m_logger(logger)
		, m_name(name)
		, m_unoodler(s_oodle)
		, m_oodler(s_oodle) {
	}

	SingleStreamWriter Write() {
		return { *this };
	}

	void Write(const void* buf, size_t length) {
		const auto uint8buf = static_cast<const uint8_t*>(buf);
		m_buffer.insert(m_buffer.end(), uint8buf, uint8buf + length);
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
		if (m_buffer.empty())
			return {};
		return {
			reinterpret_cast<const T*>(&m_buffer[m_pointer]),
			count == SIZE_MAX ? (m_buffer.size() - m_pointer) / sizeof(T) : count
		};
	}

	template<typename T = uint8_t, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	void Consume(size_t count) {
		m_pointer += count * sizeof(T);
		if (m_pointer == m_buffer.size()) {
			m_buffer.clear();
			m_pointer = 0;
		} else if (m_pointer > m_buffer.size()) {
			m_buffer.clear();
			m_pointer = 0;
			m_logger.Log(LogCategory::SocketHook, "SingleStream: overconsuming", LogLevel::Warning);
		}
	}

	template<typename T, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	size_t Read(T* buf, size_t count) {
		count = std::min(count, (m_buffer.size() - m_pointer) / sizeof(T));
		memcpy(buf, &m_buffer[m_pointer], count * sizeof(T));
		Consume<T>(count);
		return count;
	}

	template<typename T = uint8_t, typename = std::enable_if_t<std::is_standard_layout_v<T>>>
	[[nodiscard]] size_t Available() const {
		return (m_buffer.size() - m_pointer) / sizeof(T);
	}

	void TunnelXivStream(SingleStream& target, const XivAlexander::Apps::MainApp::Internal::SingleConnection::MessageMangler& messageMangler) {
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
				auto messages = pGamePacket->GetMessages(m_inflater, m_unoodler);
				auto header = *pGamePacket;
				header.TotalLength = static_cast<uint32_t>(sizeof XivBundleHeader);
				header.MessageCount = 0;
				header.CompressionType = pGamePacket->CompressionType;
				header.DecodedBodyLength = 0;

				for (auto& message : messages) {
					const auto pMessage = reinterpret_cast<XivMessage*>(&message[0]);

					if (!messageMangler(pMessage))
						pMessage->Length = 0;

					if (!pMessage->Length)
						continue;

					header.DecodedBodyLength += pMessage->Length;
					header.MessageCount += 1;
				}

				std::vector<uint8_t> body;
				body.reserve(header.DecodedBodyLength);
				for (const auto& message : messages) {
					if (!reinterpret_cast<const XivMessage*>(&message[0])->Length)
						continue;
					body.insert(body.end(), message.begin(), message.end());
				}

				std::span<uint8_t> encoded;
				switch (header.CompressionType) {
					case CompressionType::None:
						encoded = { body };
						break;
					case CompressionType::Deflate:
						encoded = m_deflater(body);
						break;
					case CompressionType::Oodle:
						encoded = m_oodler.encode(body);
						break;
					default:
						throw std::runtime_error("Unsupported compression method");
				}

				header.TotalLength += static_cast<uint32_t>(encoded.size());
				target.Write(&header, sizeof XivBundleHeader);
				target.Write(encoded);
			} catch (const std::exception& e) {
				m_logger.Format<XivAlexander::LogLevel::Warning>(XivAlexander::LogCategory::SocketHook, "{}: Error: {}\n{}", m_name, e.what(), pGamePacket->Represent());
				target.Write(pGamePacket, pGamePacket->TotalLength);
			}

			Consume(pGamePacket->TotalLength);
		}
	}
};

struct XivAlexander::Apps::MainApp::Internal::SingleConnection::Implementation {
	Internal::SingleConnection& SingleConnection;
	Internal::SocketHook& SocketHook;
	bool Detaching = false;

	std::map<size_t, std::vector<MessageMangler>> IncomingHandlers{};
	std::map<size_t, std::vector<MessageMangler>> OutgoingHandlers{};

	std::deque<uint64_t> KeepAliveRequestTimestampsUs{};
	std::deque<uint64_t> ObservedServerResponseList{};
	std::deque<int64_t> ObservedConnectionLatencyList{};

	SingleStream RecvRaw;
	SingleStream RecvProcessed;
	SingleStream SendRaw;
	SingleStream SendProcessed;

	sockaddr_storage LocalAddress = { AF_UNSPEC };
	sockaddr_storage RemoteAddress = { AF_UNSPEC };

	Utils::CallOnDestruction PingTrackKeeper;

	mutable int IoctlTcpInfoFailureCount = 0;
	uint64_t NextTcpDelaySetAttempt = 0;

	Implementation(Internal::SingleConnection& singleConnection, Internal::SocketHook& socketHook);

	void SetTCPDelay() {
		if (NextTcpDelaySetAttempt > GetTickCount64())
			return;

		DWORD buf = 0, cb = 0;

		// SIO_TCP_SET_ACK_FREQUENCY: Controls ACK delay every x ACK. Default delay is 40 or 200ms. Default value is 2 ACKs, set to 1 to disable ACK delay.
		int freq = 1;
		if (SOCKET_ERROR == WSAIoctl(SingleConnection.m_socket, SIO_TCP_SET_ACK_FREQUENCY, &freq, sizeof freq, &buf, sizeof buf, &cb, nullptr, nullptr)) {
			NextTcpDelaySetAttempt = GetTickCount64() + 1000;
			throw Utils::Win32::Error(WSAGetLastError(), "WSAIoctl(SIO_TCP_SET_ACK_FREQUENCY, 1, 0)");
		}

		// TCP_NODELAY: if enabled, sends packets as soon as possible instead of waiting for ACK or large packet.
		int optval = 1;
		if (SOCKET_ERROR == setsockopt(SingleConnection.m_socket, SOL_SOCKET, TCP_NODELAY, reinterpret_cast<char*>(&optval), sizeof optval)) {
			NextTcpDelaySetAttempt = GetTickCount64() + 1000;
			throw Utils::Win32::Error(WSAGetLastError(), "setsockopt(TCP_NODELAY, 1)");
		}

		NextTcpDelaySetAttempt = GetTickCount64();
	}

	void ResolveAddresses();

	void AttemptReceive() {
		if (auto write = RecvRaw.Write();
			!write.Write(std::max(0, SocketHook.recv.bridge(SingleConnection.m_socket, write.Allocate<char>(65536), 65536, 0))))
			return;

		ProcessRecvData();
	}

	void AttemptSend() {
		const auto data = SendProcessed.Peek<char>();
		if (data.empty())
			return;

		const auto sent = SocketHook.send.bridge(SingleConnection.m_socket, data.data(), static_cast<int>(data.size_bytes()), 0);
		if (sent == SOCKET_ERROR)
			return;

		SendProcessed.Consume(sent);
	}

	void ProcessRecvData() {
		RecvRaw.TunnelXivStream(RecvProcessed, [&](auto* pMessage) {
			auto use = true;

			switch (pMessage->Type) {
				case MessageType::ServerKeepAlive:
					if (!KeepAliveRequestTimestampsUs.empty()) {
						int64_t delayUs;
						do {
							delayUs = static_cast<int64_t>(Utils::QpcUs() - KeepAliveRequestTimestampsUs.front());
							KeepAliveRequestTimestampsUs.pop_front();
						} while (!KeepAliveRequestTimestampsUs.empty() && delayUs > 5000000);

						// Add statistics sample
						SingleConnection.ApplicationLatencyUs.AddValue(delayUs);
						if (const auto latency = SingleConnection.FetchSocketLatencyUs())
							SingleConnection.SocketLatencyUs.AddValue(*latency);
					}
					break;

				case MessageType::Ipc:
					for (const auto& cbs : IncomingHandlers) {
						for (const auto& cb : cbs.second) {
							use &= cb(pMessage);
						}
					}
			}

			return use;
			});
	}

	void ProcessSendData() {
		SendRaw.TunnelXivStream(SendProcessed, [&](auto* pMessage) {
			auto use = true;

			switch (pMessage->Type) {
				case MessageType::ClientKeepAlive:
					KeepAliveRequestTimestampsUs.push_back(Utils::QpcUs());
					break;

				case MessageType::Ipc:
					for (const auto& cbs : OutgoingHandlers) {
						for (const auto& cb : cbs.second) {
							use &= cb(pMessage);
						}
					}
			}

			return use;
			});
	}

	bool CanCompleteDetach() const {
		return !RecvRaw.Available() && !RecvProcessed.Available() && !SendRaw.Available() && !SendProcessed.Available() && Detaching;
	}
};

struct XivAlexander::Apps::MainApp::Internal::SocketHook::Implementation {
	const std::shared_ptr<XivAlexander::Config> Config;
	Internal::SocketHook& SocketHook;
	Apps::MainApp::App& App;
	const DWORD GameMainThreadId;
	Misc::IcmpPingTracker PingTracker;
	std::map<SOCKET, std::unique_ptr<SingleConnection>> Sockets;
	std::set<SOCKET> NonGameSockets;
	std::vector<std::pair<uint32_t, uint32_t>> AllowedIpRange;
	std::vector<std::pair<uint32_t, uint32_t>> AllowedPortRange;
	Utils::CallOnDestruction::Multiple Cleanup;

	Implementation(Internal::SocketHook& socketHook, Apps::MainApp::App& app)
		: Config(XivAlexander::Config::Acquire())
		, SocketHook(socketHook)
		, App(app)
		, GameMainThreadId(app.GetGameWindowThreadId()) {
		auto reparse = [this]() {
			ParseTakeOverAddresses();
			SocketHook.ReleaseSockets();
		};
		Cleanup += Config->Game.Server_IpRange.OnChange(reparse);
		Cleanup += Config->Game.Server_PortRange.OnChange(reparse);
		Cleanup += Config->Runtime.TakeOverAllAddresses.OnChange(reparse);
		Cleanup += Config->Runtime.TakeOverPrivateAddresses.OnChange(reparse);
		Cleanup += Config->Runtime.TakeOverLoopbackAddresses.OnChange(reparse);
		Cleanup += Config->Runtime.TakeOverAllPorts.OnChange(reparse);
		ParseTakeOverAddresses();
	}

	~Implementation() = default;

	void ParseTakeOverAddresses() {
		const auto& game = Config->Game;
		const auto& runtime = Config->Runtime;
		try {
			AllowedIpRange = Utils::ParseIpRange(game.Server_IpRange, runtime.TakeOverAllAddresses, runtime.TakeOverPrivateAddresses, runtime.TakeOverLoopbackAddresses);
		} catch (const std::exception& e) {
			SocketHook.m_logger->Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
		}
		try {
			AllowedPortRange = Utils::ParsePortRange(game.Server_PortRange, runtime.TakeOverAllPorts);
		} catch (const std::exception& e) {
			SocketHook.m_logger->Format<LogLevel::Error>(LogCategory::SocketHook, e.what());
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
			SocketHook.m_logger->Format(LogCategory::SocketHook, Config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_IGNORED_NOT_IPV4, s);
			NonGameSockets.emplace(s);
			return TestRemoteAddressResult::RegisterIgnore;
		}

		if (!AllowedIpRange.empty()) {
			const uint32_t ip = ntohl(addr.sin_addr.s_addr);
			bool pass = false;
			for (const auto& range : AllowedIpRange) {
				if (range.first <= ip && ip <= range.second) {
					pass = true;
					break;
				}
			}
			if (!pass) {
				SocketHook.m_logger->Format(LogCategory::SocketHook, Config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_IGNORED_IP, s, Utils::ToString(addr));
				return TestRemoteAddressResult::RegisterIgnore;
			}
		}
		if (!AllowedPortRange.empty()) {
			bool pass = false;
			for (const auto& range : AllowedPortRange) {
				if (range.first <= addr.sin_port && addr.sin_port <= range.second) {
					pass = true;
					break;
				}
			}
			if (!pass) {
				SocketHook.m_logger->Format(LogCategory::SocketHook, Config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_IGNORED_PORT, s, Utils::ToString(addr));
				return TestRemoteAddressResult::RegisterIgnore;
			}
		}
		return TestRemoteAddressResult::TakeOver;
	}

	SingleConnection* FindOrCreateSingleConnection(SOCKET s, bool existingOnly = false) {
		if (const auto found = Sockets.find(s); found != Sockets.end()) {
			found->second->ResolveAddresses();
			return found->second.get();
		}
		if (SocketHook.m_unloading
			|| existingOnly
			|| NonGameSockets.find(s) != NonGameSockets.end())
			return nullptr;

		switch (TestRemoteAddressAndLog(s)) {
			case TestRemoteAddressResult::Pass:
				return nullptr;
			case TestRemoteAddressResult::RegisterIgnore:
				NonGameSockets.emplace(s);
				return nullptr;
			case TestRemoteAddressResult::TakeOver:
				break;
		}
		Sockets.emplace(s, std::make_unique<SingleConnection>(SocketHook, s));
		const auto ptr = Sockets.at(s).get();
		SocketHook.OnSocketFound(*ptr);
		return ptr;
	}

	decltype(Sockets.end()) CleanupSocket(decltype(Sockets.end()) it) {
		if (it == Sockets.end())
			return it;
		SocketHook.OnSocketGone(*it->second);
		return Sockets.erase(it);
	}

	decltype(Sockets.end()) CleanupSocket(SOCKET s) {
		return CleanupSocket(Sockets.find(s));
	}
};

void XivAlexander::Apps::MainApp::Internal::SingleConnection::Implementation::ResolveAddresses() {
	sockaddr_storage local{}, remote{};
	socklen_t addrlen = sizeof local;
	if (0 == getsockname(SingleConnection.m_socket, reinterpret_cast<sockaddr*>(&local), &addrlen) && Utils::CompareSockaddr(&LocalAddress, &local)) {
		LocalAddress = local;
		SocketHook.m_logger->Format(LogCategory::SocketHook, "{:x}: Local={}", SingleConnection.m_socket, Utils::ToString(local));

		// Set TCP delay here because SIO_TCP_SET_ACK_FREQUENCY seems to work only when localAddress is not 0.0.0.0.
		if (SocketHook.m_pImpl->Config->Runtime.ReducePacketDelay && reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr != INADDR_ANY) {
			try {
				SetTCPDelay();
			} catch (const Utils::Win32::Error&) {
				// don't print error
			}
		}
	}
	addrlen = sizeof remote;
	if (0 == getpeername(SingleConnection.m_socket, reinterpret_cast<sockaddr*>(&remote), &addrlen) && Utils::CompareSockaddr(&RemoteAddress, &remote)) {
		RemoteAddress = remote;
		SocketHook.m_logger->Format(LogCategory::SocketHook, "{:x}: Remote={}", SingleConnection.m_socket, Utils::ToString(remote));
	}

	const auto& local4 = *reinterpret_cast<const sockaddr_in*>(&LocalAddress);
	const auto& remote4 = *reinterpret_cast<const sockaddr_in*>(&RemoteAddress);
	if (local4.sin_family == AF_INET && remote4.sin_family == AF_INET && local4.sin_addr.s_addr && remote4.sin_addr.s_addr && !PingTrackKeeper)
		PingTrackKeeper = SocketHook.m_pImpl->PingTracker.Track(
			reinterpret_cast<sockaddr_in*>(&LocalAddress)->sin_addr,
			reinterpret_cast<sockaddr_in*>(&RemoteAddress)->sin_addr
		);
}

XivAlexander::Apps::MainApp::Internal::SingleConnection::Implementation::Implementation(Internal::SingleConnection& singleConnection, Internal::SocketHook& socketHook)
	: SingleConnection(singleConnection)
	, SocketHook(socketHook)
	, RecvRaw(*socketHook.m_logger, "S2C_Raw")
	, RecvProcessed(*socketHook.m_logger, "S2C_Processed")
	, SendRaw(*socketHook.m_logger, "C2S_Raw")
	, SendProcessed(*socketHook.m_logger, "C2S_Processed") {
	socketHook.m_logger->Format(LogCategory::SocketHook, socketHook.m_pImpl->Config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_FOUND, SingleConnection.m_socket);
	ResolveAddresses();
}

XivAlexander::Apps::MainApp::Internal::SingleConnection::SingleConnection(Internal::SocketHook& hook, SOCKET s)
	: m_socket(s)
	, m_pImpl(std::make_unique<Implementation>(*this, hook)) {
}

XivAlexander::Apps::MainApp::Internal::SingleConnection::~SingleConnection() = default;

void XivAlexander::Apps::MainApp::Internal::SingleConnection::AddIncomingFFXIVMessageHandler(void* token, MessageMangler cb) {
	m_pImpl->IncomingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void XivAlexander::Apps::MainApp::Internal::SingleConnection::AddOutgoingFFXIVMessageHandler(void* token, MessageMangler cb) {
	m_pImpl->OutgoingHandlers[reinterpret_cast<size_t>(token)].emplace_back(std::move(cb));
}

void XivAlexander::Apps::MainApp::Internal::SingleConnection::RemoveMessageHandlers(void* token) {
	m_pImpl->IncomingHandlers.erase(reinterpret_cast<size_t>(token));
	m_pImpl->OutgoingHandlers.erase(reinterpret_cast<size_t>(token));
}

void XivAlexander::Apps::MainApp::Internal::SingleConnection::ResolveAddresses() {
	m_pImpl->ResolveAddresses();
}

std::optional<int64_t> XivAlexander::Apps::MainApp::Internal::SingleConnection::FetchSocketLatencyUs() {
	// Give up after 5 consecutive failures on measuring socket latency
	if (m_pImpl->IoctlTcpInfoFailureCount >= 5)
		return {};

	TCP_INFO_v0 info{};
	DWORD tcpInfoVersion = 0, cb = 0;
	if (0 != WSAIoctl(m_socket, SIO_TCP_INFO, &tcpInfoVersion, sizeof tcpInfoVersion, &info, sizeof info, &cb, nullptr, nullptr)) {
		const auto err = WSAGetLastError();
		m_pImpl->SocketHook.m_logger->Format<LogLevel::Info>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0: {:08x} ({})", m_socket,
			err, Utils::Win32::FormatWindowsErrorMessage(err));
		m_pImpl->IoctlTcpInfoFailureCount++;
		return {};

	} else if (cb != sizeof info) {
		m_pImpl->SocketHook.m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "{:x}: WSAIoctl SIO_TCP_INFO v0: buffer size mismatch ({} != {})", m_socket, cb, sizeof info);
		m_pImpl->IoctlTcpInfoFailureCount++;
		return {};

	} else {
		const auto latency = static_cast<int64_t>(info.RttUs);
		SocketLatencyUs.AddValue(latency);
		return std::make_optional(latency);
	}
};

const Utils::NumericStatisticsTracker* XivAlexander::Apps::MainApp::Internal::SingleConnection::GetPingLatencyTrackerUs() const {
	if (m_pImpl->LocalAddress.ss_family != AF_INET || m_pImpl->RemoteAddress.ss_family != AF_INET)
		return nullptr;
	const auto& local = *reinterpret_cast<const sockaddr_in*>(&m_pImpl->LocalAddress);
	const auto& remote = *reinterpret_cast<const sockaddr_in*>(&m_pImpl->RemoteAddress);
	if (!local.sin_addr.s_addr || !remote.sin_addr.s_addr)
		return nullptr;
	return m_pImpl->SocketHook.m_pImpl->PingTracker.GetTrackerUs(local.sin_addr, remote.sin_addr);
}

XivAlexander::Apps::MainApp::Internal::SocketHook::SocketHook(Apps::MainApp::App & app)
	: m_logger(Misc::Logger::Acquire())
	, OnSocketFound([this](const auto& cb) { if (m_pImpl) { for (const auto& val : m_pImpl->Sockets | std::views::values) cb(*val); } }) {

#ifdef _WIN64
	if (const auto oodleFirst = Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
		"\x48\x83\x7b\x00\x00\x75\x00\xb9\x00\x00\x00\x00\xe8\x00\x00\x00\x00\x45\x33\xc0\x33\xd2\x48\x8b\xc8\xe8",
		"\xff\xff\xff\x00\xff\xff\x00\xff\x00\xff\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff",
		26, {}); !oodleFirst.empty()) {
		static constexpr size_t htbitsIndex = 8;
#else
	// 83 7e ?? ?? 75 ?? 6a 13 e8 ?? ?? ?? ?? 6a 00 6a 00 50 e8
	if (const auto oodleFirst = Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
		"\x83\x7e\x00\x00\x75\x00\x6a\x00\xe8\x00\x00\x00\x00\x6a\x00\x6a\x00\x50\xe8",
		"\xff\xff\x00\x00\xff\x00\xff\x00\xff\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff",
		19, {}); !oodleFirst.empty()) {
		static constexpr size_t htbitsIndex = 7;
#endif

		try {
			ZydisDecoder decoder;
			ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);

			auto basePtr = static_cast<uint8_t*>(oodleFirst.front());
			s_oodle.htbits = basePtr[htbitsIndex];

			std::vector<void*> callTargetAddresses;
			ZydisDecodedInstruction instruction;
			for (
				size_t offset = 0, funclen = 32768;
				callTargetAddresses.size() < 6 && ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(&decoder, &basePtr[offset], funclen - offset, &instruction));
				offset += instruction.length
				) {
				if (instruction.meta.category != ZYDIS_CATEGORY_CALL)
					continue;
				if (uint64_t resultAddress;
					instruction.operand_count >= 1
					&& ZYAN_STATUS_SUCCESS == ZydisCalcAbsoluteAddress(&instruction, &instruction.operands[0],
						reinterpret_cast<size_t>(&basePtr[offset]), &resultAddress)) {

					callTargetAddresses.push_back(reinterpret_cast<void*>(static_cast<size_t>(resultAddress)));
				}
			}

			s_oodle.OodleNetwork1_Shared_Size = static_cast<Utils::OodleNetwork1_Shared_Size*>(callTargetAddresses[0]);
			s_oodle.OodleNetwork1_Shared_SetWindow = static_cast<Utils::OodleNetwork1_Shared_SetWindow*>(callTargetAddresses[2]);
			basePtr = static_cast<uint8_t*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\xcc\xb8\x00\xb4\x2e\x00\xc3",
				"\xff\xff\xff\xff\xff\xff",
				6, {}).front()) + 1;

			s_oodle.OodleNetwork1UDP_State_Size = reinterpret_cast<Utils::OodleNetwork1UDP_State_Size*>(basePtr);

#ifdef _WIN64
			s_oodle.OodleNetwork1UDP_Train = static_cast<Utils::OodleNetwork1UDP_Train*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7c\x24\x20\x41\x56\x48\x83\xec\x30\x48\x8b\xf2",
				"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
				29, {}).front());

			s_oodle.OodleNetwork1UDP_Decode = static_cast<Utils::OodleNetwork1UDP_Decode*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\x40\x53\x48\x83\xec\x00\x48\x8b\x44\x24\x68\x49\x8b\xd9\x48\x85\xc0\x7e",
				"\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
				18, {}).front());

			s_oodle.OodleNetwork1UDP_Encode = reinterpret_cast<Utils::OodleNetwork1UDP_Encode*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\x4c\x89\x4c\x24\x20\x4c\x89\x44\x24\x18\x48\x89\x4c\x24\x08\x55\x56\x57\x41\x55\x41\x57\x48\x8d\x6c\x24\xd1",
				"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
				27, {}).front());
#else
			s_oodle.OodleNetwork1UDP_Train = static_cast<Utils::OodleNetwork1UDP_Train*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\x56\x6a\x08\x68\x00\x84\x4a\x00",
				"\xff\xff\xff\xff\xff\xff\xff\xff",
				8, {}).front());

			s_oodle.OodleNetwork1UDP_Decode = static_cast<Utils::OodleNetwork1UDP_Decode*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\x8b\x44\x24\x18\x56\x85\xc0\x7e\x00\x8b\x74\x24\x14\x85\xf6\x7e\x00\x3b\xf0",
				"\xff\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff",
				19, {}).front());

			s_oodle.OodleNetwork1UDP_Encode = reinterpret_cast<Utils::OodleNetwork1UDP_Encode*>(Misc::Hooks::LookupForData(&Misc::Hooks::SectionFilterTextOnly,
				"\xff\x74\x24\x14\x8b\x4c\x24\x08\xff\x74\x24\x14\xff\x74\x24\x14\xff\x74\x24\x14\xe8\x00\x00\x00\x00\xc2\x14\x00\xcc\xcc\xcc\xcc\xb8",
				"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff",
				33, {}).front());
			
#endif
			s_oodle.found = true;
		} catch (const std::exception& e) {
			m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "Failed to find oodle stuff: {}", e.what());
		}
	} else {
		m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "Oodle signatures could not be found.");
	}

	m_hThreadSetupHook = Utils::Win32::Thread(L"SocketHook::SocketHook/WaitGameWindow", [this, &app]() {
		m_logger->Log(LogCategory::SocketHook, "Waiting for game window to stabilize before setting up redirecting network operations.");
		app.RunOnGameLoop([&]() {
			m_pImpl = std::make_unique<Implementation>(*this, app);
			if (m_unloading)
				return;

			void(Utils::Win32::Thread(L"SocketHook::SocketHook/InitRunner", [this, &app]() {
				app.RunOnGameLoop([&]() {
					try {
						m_pImpl->Cleanup += std::move(socket.SetHook([&](_In_ int af, _In_ int type, _In_ int protocol) {
							const auto result = socket.bridge(af, type, protocol);
							if (GetCurrentThreadId() == m_pImpl->GameMainThreadId) {
								m_logger->Format(LogCategory::SocketHook, "{:x}: API(socket)", result);
								m_pImpl->FindOrCreateSingleConnection(result);
							}
							return result;
							}).Wrap([&app](auto fn) { app.RunOnGameLoop(std::move(fn)); }));

						m_pImpl->Cleanup += std::move(closesocket.SetHook([&](SOCKET s) {
							if (GetCurrentThreadId() != m_pImpl->GameMainThreadId)
								return closesocket.bridge(s);

							m_pImpl->CleanupSocket(s);
							m_logger->Format(LogCategory::SocketHook, "{:x}: API(closesocket)", s);
							m_pImpl->NonGameSockets.erase(s);
							return closesocket.bridge(s);
							}).Wrap([&app](auto fn) { app.RunOnGameLoop(std::move(fn)); }));

						m_pImpl->Cleanup += std::move(send.SetHook([&](SOCKET s, const char* buf, int len, int flags) {
							if (GetCurrentThreadId() != m_pImpl->GameMainThreadId)
								return send.bridge(s, buf, len, flags);

							const auto conn = m_pImpl->FindOrCreateSingleConnection(s);
							if (conn == nullptr)
								return send.bridge(s, buf, len, flags);

							conn->m_pImpl->SendRaw.Write(buf, len);
							conn->m_pImpl->ProcessSendData();
							conn->m_pImpl->AttemptSend();
							return len;
							}).Wrap([&app](auto fn) { app.RunOnGameLoop(std::move(fn)); }));

						m_pImpl->Cleanup += std::move(recv.SetHook([&](SOCKET s, char* buf, int len, int flags) {
							if (GetCurrentThreadId() != m_pImpl->GameMainThreadId)
								return recv.bridge(s, buf, len, flags);

							const auto conn = m_pImpl->FindOrCreateSingleConnection(s);
							if (conn == nullptr)
								return recv.bridge(s, buf, len, flags);

							conn->m_pImpl->AttemptReceive();

							const auto result = conn->m_pImpl->RecvProcessed.Read(reinterpret_cast<uint8_t*>(buf), len);
							if (conn->m_pImpl->CanCompleteDetach())
								m_pImpl->CleanupSocket(s);

							return static_cast<int>(result);
							}).Wrap([&app](auto fn) { app.RunOnGameLoop(std::move(fn)); }));

						m_pImpl->Cleanup += std::move(select.SetHook([&](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) {
							if (GetCurrentThreadId() != m_pImpl->GameMainThreadId)
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

								if (conn->m_pImpl->RecvProcessed.Available())
									FD_SET(s, readfds);

								if (conn->m_pImpl->CanCompleteDetach())
									m_pImpl->CleanupSocket(s);
							}

							for (auto it = m_pImpl->Sockets.begin(); it != m_pImpl->Sockets.end();) {
								it->second->m_pImpl->AttemptSend();

								if (it->second->m_pImpl->CanCompleteDetach())
									it = m_pImpl->CleanupSocket(it->first);
								else
									++it;
							}

							return static_cast<int>((readfds ? readfds->fd_count : 0) +
								(writefds ? writefds->fd_count : 0) +
								(exceptfds ? exceptfds->fd_count : 0));
							}).Wrap([&app](auto fn) { app.RunOnGameLoop(std::move(fn)); }));

						m_pImpl->Cleanup += std::move(connect.SetHook([&](SOCKET s, const sockaddr* name, int namelen) {
							if (GetCurrentThreadId() != m_pImpl->GameMainThreadId)
								return connect.bridge(s, name, namelen);

							const auto result = connect.bridge(s, name, namelen);
							m_logger->Format(LogCategory::SocketHook, "{:x}: API(connect): {}", s, Utils::ToString(*name));
							return result;
							}).Wrap([&app](auto fn) { app.RunOnGameLoop(std::move(fn)); }));
						m_logger->Log(LogCategory::SocketHook, "Network operation has been redirected.");

					} catch (const std::exception& e) {
						m_logger->Format<LogLevel::Warning>(LogCategory::SocketHook, "Failed to redirect network operation: {}", e.what());
					}
					});
				}));
			});
		});
}

XivAlexander::Apps::MainApp::Internal::SocketHook::~SocketHook() {
	m_unloading = true;
	m_hThreadSetupHook.Wait();

	if (!m_pImpl)
		return;

	while (!m_pImpl->Sockets.empty()) {
		m_pImpl->App.RunOnGameLoop([this]() { ReleaseSockets(); });
		Sleep(1);
	}

	if (const auto hGameWnd = m_pImpl->App.GetGameWindowHandle(false)) {
		// Let it process main message loop first to ensure that no socket operation is in progress
		SendMessageW(hGameWnd, WM_NULL, 0, 0);
	}
	m_pImpl->Cleanup.Clear();

}

bool XivAlexander::Apps::MainApp::Internal::SocketHook::IsUnloadable() const {
	return socket.IsDisableable()
		&& connect.IsDisableable()
		&& select.IsDisableable()
		&& recv.IsDisableable()
		&& send.IsDisableable()
		&& closesocket.IsDisableable();
}

void XivAlexander::Apps::MainApp::Internal::SocketHook::ReleaseSockets() {
	if (!m_pImpl)
		return;

	for (const auto& entry : m_pImpl->Sockets) {
		if (entry.second->m_pImpl->Detaching)
			continue;

		m_logger->Format(LogCategory::SocketHook, m_pImpl->Config->Runtime.GetLangId(), IDS_SOCKETHOOK_SOCKET_DETACH, entry.first);
		entry.second->m_pImpl->Detaching = true;
	}
	m_pImpl->NonGameSockets.clear();
}

std::wstring XivAlexander::Apps::MainApp::Internal::SocketHook::Describe() const {
	if (!m_pImpl)
		return {};

	while (true) {
		try {
			std::wstring result;
			for (const auto& entry : m_pImpl->Sockets) {
				const auto& conn = entry.second;
				result += m_pImpl->Config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_TITLE,
					entry.first,
					Utils::FromUtf8(Utils::ToString(conn->m_pImpl->LocalAddress)),
					Utils::FromUtf8(Utils::ToString(conn->m_pImpl->RemoteAddress)));

				if (const auto latency = conn->FetchSocketLatencyUs()) {
					const auto [mean, dev] = conn->SocketLatencyUs.MeanAndDeviation();
					result += m_pImpl->Config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_SOCKET_LATENCY,
						*latency, conn->SocketLatencyUs.Median(), mean, dev);
				} else
					result += m_pImpl->Config->Runtime.GetStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_SOCKET_LATENCY_FAILURE);

				if (const auto tracker = conn->GetPingLatencyTrackerUs(); tracker && tracker->Count()) {
					const auto [mean, dev] = tracker->MeanAndDeviation();
					result += m_pImpl->Config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_PING_LATENCY,
						tracker->Latest(), tracker->Median(), mean, dev);
				} else
					result += m_pImpl->Config->Runtime.GetStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_PING_LATENCY_FAILURE);

				{
					const auto [mean, dev] = conn->ApplicationLatencyUs.MeanAndDeviation();
					result += m_pImpl->Config->Runtime.FormatStringRes(IDS_SOCKETHOOK_SOCKET_DESCRIBE_RESPONSE_DELAY,
						conn->ApplicationLatencyUs.Median(), mean, dev);
				}
			}
			return result;
		} catch (...) {
			// ignore
		}
	}
}
