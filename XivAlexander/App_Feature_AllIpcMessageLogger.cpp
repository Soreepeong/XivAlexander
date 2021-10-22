#include "pch.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Misc_Logger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

struct App::Feature::AllIpcMessageLogger::Implementation {
	class SingleConnectionHandler {
	public:
		Implementation* Impl;
		Network::SingleConnection& Conn;

		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: Impl(pImpl)
			, Conn(conn) {
			using namespace Network::Structures;

			Conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				Impl->m_logger->Format(LogCategory::AllIpcMessageLogger, "Incoming: {}", pMessage->DebugPrint({ .Dump = true, .DumpString = true, .Guess = FFXIVMessage::DumpConfig::Incoming }));
				return true;
			});
			Conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				Impl->m_logger->Format(LogCategory::AllIpcMessageLogger, "Outgoing: {}", pMessage->DebugPrint({ .Dump = true, .DumpString = true, .Guess = FFXIVMessage::DumpConfig::Outgoing }));
				return true;
			});
		}

		~SingleConnectionHandler() {
			Conn.RemoveMessageHandlers(this);
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
