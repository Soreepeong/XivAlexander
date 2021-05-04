#include "pch.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

class App::Feature::AllIpcMessageLogger::Internals {
public:

	class SingleConnectionHandler {
	public:
		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace Network::Structures;

			conn.AddIncomingFFXIVMessageHandler(this, [&](FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					const char* pszPossibleMessageType;
					switch (pMessage->Length) {
						case 0x09c: pszPossibleMessageType = "AttackResponse01"; break;
						case 0x29c: pszPossibleMessageType = "AttackResponse08"; break;
						case 0x4dc: pszPossibleMessageType = "AttackResponse16"; break;
						case 0x71c: pszPossibleMessageType = "AttackResponse24"; break;
						case 0x95c: pszPossibleMessageType = "AttackResponse32"; break;
						case 0x040: pszPossibleMessageType = "ActorControlSelf, ActorCast"; break;
						case 0x038: pszPossibleMessageType = "ActorControl"; break;
						case 0x078: pszPossibleMessageType = "AddStatusEffect"; break;
						default: pszPossibleMessageType = nullptr;
					}
					Misc::Logger::GetLogger().Format(LogCategory::AllIpcMessageLogger, "source=%08x current=%08x subtype=%04x length=%x (S2C%s%s)",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.IPC.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "");
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					const char* pszPossibleMessageType;
					switch (pMessage->Length) {
						case 0x038: pszPossibleMessageType = "PositionUpdate"; break;
						case 0x040: pszPossibleMessageType = "ActionRequest, ActionRequest2, InteractTarget"; break;
						default: pszPossibleMessageType = nullptr;
					}
					Misc::Logger::GetLogger().Format(LogCategory::AllIpcMessageLogger, "source=%08x current=%08x subtype=%04x length=%x (C2S%s%s)",
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

	std::map<Network::SingleConnection*, std::unique_ptr<SingleConnectionHandler>> m_handlers;

	Internals() {
		Network::SocketHook::Instance()->AddOnSocketFoundListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
		});
		Network::SocketHook::Instance()->AddOnSocketGoneListener(this, [&](Network::SingleConnection& conn) {
			m_handlers.erase(&conn);
			});
	}

	~Internals() {
		m_handlers.clear();
		Network::SocketHook::Instance()->RemoveListeners(this);
	}
};

App::Feature::AllIpcMessageLogger::AllIpcMessageLogger()
: impl(std::make_unique<Internals>()){
}

App::Feature::AllIpcMessageLogger::~AllIpcMessageLogger() {
}
