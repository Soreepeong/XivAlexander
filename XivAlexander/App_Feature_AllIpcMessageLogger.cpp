#include "pch.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::AllIpcMessageLogger::Implementation {
public:

	class SingleConnectionHandler {
	public:
		Implementation* m_pImpl;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Implementation* pImpl, Network::SingleConnection& conn)
			: m_pImpl(pImpl)
			, conn(conn) {
			using namespace Network::Structures;

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					const char* pszPossibleMessageType;
					switch (pMessage->Length) {
						case 0x09c: pszPossibleMessageType = "ActionEffect01"; break;
						case 0x29c: pszPossibleMessageType = "ActionEffect08"; break;
						case 0x4dc: pszPossibleMessageType = "ActionEffect16"; break;
						case 0x71c: pszPossibleMessageType = "ActionEffect24"; break;
						case 0x95c: pszPossibleMessageType = "ActionEffect32"; break;
						case 0x040: pszPossibleMessageType = "ActorControlSelf, ActorCast"; break;
						case 0x038: pszPossibleMessageType = "ActorControl"; break;
						case 0x078: pszPossibleMessageType = "AddStatusEffect"; break;
						default: pszPossibleMessageType = nullptr;
					}
					m_pImpl->m_logger->Format(LogCategory::AllIpcMessageLogger, "source={:08x} current={:08x} subtype={:04x} length={:x} (S2C{}{})",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.IPC.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "");
				}
				return true;
			});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					const char* pszPossibleMessageType;
					switch (pMessage->Length) {
						case 0x038: pszPossibleMessageType = "PositionUpdate"; break;
						case 0x040: pszPossibleMessageType = "ActionRequest, C2S_ActionRequestGroundTargeted, InteractTarget"; break;
						default: pszPossibleMessageType = nullptr;
					}
					m_pImpl->m_logger->Format(LogCategory::AllIpcMessageLogger, "source={:08x} current={:08x} subtype={:04x} length={:x} (C2S{}{})",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.IPC.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "");
				}
				return true;
			});
		}
		~SingleConnectionHandler() {
			conn.RemoveMessageHandlers(this);
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
