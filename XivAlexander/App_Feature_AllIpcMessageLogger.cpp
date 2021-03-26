#include "pch.h"
#include "App_Feature_AllIpcMessageLogger.h"
#include "App_Network_SocketHook.h"
#include "App_Network_Structures.h"

static const float ExtraDelay = 0.1f;

class App::Feature::AllIpcMessageLogger::Internals {
public:

	class SingleConnectionHandler {
	public:
		Internals& internals;
		Network::SingleConnection& conn;
		SingleConnectionHandler(Internals& internals, Network::SingleConnection& conn)
			: internals(internals)
			, conn(conn) {
			using namespace App::Network::Structures;

			conn.AddIncomingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					const char* expected;
					switch (pMessage->Length) {
						case 0x09c: expected = "AttackResponse01"; break;
						case 0x29c: expected = "AttackResponse08"; break;
						case 0x4dc: expected = "AttackResponse16"; break;
						case 0x71c: expected = "AttackResponse24"; break;
						case 0x95c: expected = "AttackResponse32"; break;
						case 0x040: expected = "ActorControlSelf/ActorCast"; break;
						case 0x038: expected = "ActorControl"; break;
						case 0x078: expected = "AddStatusEffect"; break;
						default: expected = "?";
					}
					Misc::Logger::GetLogger().Format(LogCategory::AllIpcMessageLogger, "source=%08x current=%08x subtype=%04x length=%x (%s)",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.IPC.SubType, pMessage->Length,
						expected);
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](Network::Structures::FFXIVMessage* pMessage, std::vector<uint8_t>&) {
				if (pMessage->Type == SegmentType::IPC && pMessage->Data.IPC.Type == IpcType::InterestedType) {
					// todo?
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
