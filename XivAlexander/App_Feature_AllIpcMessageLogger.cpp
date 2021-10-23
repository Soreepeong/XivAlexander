#include "pch.h"
#include "App_ConfigRepository.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

static const char HexRepresentation[]{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

struct App::Feature::AllIpcMessageLogger::Implementation {
	class SingleConnectionHandler {
	public:
		const std::shared_ptr<Config> Conf;
		Implementation* Impl;
		Network::SingleConnection& Conn;

		Utils::Win32::Handle DumpFile;
		Utils::CallOnDestruction::Multiple Cleanup;

		std::string Buffer;
		uint64_t DumpFilePointer{};

		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: Conf(Config::Acquire())
			, Impl(pImpl)
			, Conn(conn) {
			using namespace Network::Structures;

			Cleanup += Conf->Runtime.DumpAllMessagesIntoFile.OnChangeListener([this](auto& v) {
				SetupDumpFile();
			});
			SetupDumpFile();

			Conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (Conf->Runtime.PrintAllMessagesIntoLogWindow)
					Impl->m_logger->Format(LogCategory::AllIpcMessageLogger, "Incoming: {}", pMessage->DebugPrint({ .Guess = FFXIVMessage::DumpConfig::Incoming }));
				DumpMessage(pMessage, '>');
				return true;
			});
			Conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (Conf->Runtime.PrintAllMessagesIntoLogWindow)
					Impl->m_logger->Format(LogCategory::AllIpcMessageLogger, "Outgoing: {}", pMessage->DebugPrint({ .Guess = FFXIVMessage::DumpConfig::Outgoing }));
				DumpMessage(pMessage, '<');
				return true;
			});
		}

		~SingleConnectionHandler() {
			Conn.RemoveMessageHandlers(this);
		}

		void SetupDumpFile() {
			auto dumpPath = Conf->Init.ResolveConfigStorageDirectoryPath() / "Dump";
			try {
				if (Conf->Runtime.DumpAllMessagesIntoFile) {
					if (!DumpFile) {
						create_directories(dumpPath);

						SYSTEMTIME st{};
						GetLocalTime(&st);
						dumpPath /= std::format("Dump_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}_{:x}.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, static_cast<size_t>(Conn.Socket()));

						DumpFile = Utils::Win32::Handle::FromCreateFile(dumpPath,
							GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS);
						DumpFilePointer = DumpFile.GetFileSize();
					}
				} else
					DumpFile.Clear();
			} catch (const std::exception& e) {
				Impl->m_logger->Format<LogLevel::Error>(LogCategory::AllIpcMessageLogger, "Could not open file {} for message dump: {}", dumpPath, e.what());
			}
		}

		void DumpMessage(const Network::Structures::FFXIVMessage* pMessage, char type) {
			if (const auto dumpFile = DumpFile; DumpFile && Conf->Runtime.DumpAllMessagesIntoFile) {
				SYSTEMTIME st{};
				GetLocalTime(&st);
				Buffer = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, type);
				Buffer.reserve(Buffer.size() + pMessage->Length * static_cast<size_t>(2) + 1);
				for (const auto b : std::span(reinterpret_cast<const uint8_t*>(pMessage), pMessage->Length)) {
					Buffer.push_back(HexRepresentation[b >> 4]);
					Buffer.push_back(HexRepresentation[b & 0xF]);
				}
				Buffer.push_back('\n');
				try {
					DumpFilePointer += dumpFile.Write(DumpFilePointer, std::span(Buffer));
				} catch (const std::exception& e) {
					Impl->m_logger->Format<LogLevel::Error>(LogCategory::AllIpcMessageLogger, "Failed to write to message dump file: {}", e.what());
					DumpFile = nullptr;
				}
			}
		}
	};

	const std::shared_ptr<Misc::Logger> m_logger;
	Network::SocketHook* const m_socketHook;
	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;
	Utils::CallOnDestruction::Multiple m_cleanup;

	Implementation(Network::SocketHook* socketHook)
		: m_logger(Misc::Logger::Acquire())
		, m_socketHook(socketHook) {
		m_cleanup += m_socketHook->OnSocketFound([&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(this, conn));
		});
		m_cleanup += m_socketHook->OnSocketGone([&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
		});
	}

	~Implementation() {
		m_handlers.clear();
	}
};

App::Feature::AllIpcMessageLogger::AllIpcMessageLogger(Network::SocketHook* socketHook)
	: m_pImpl(std::make_unique<Implementation>(socketHook)) {
}

App::Feature::AllIpcMessageLogger::~AllIpcMessageLogger() = default;
