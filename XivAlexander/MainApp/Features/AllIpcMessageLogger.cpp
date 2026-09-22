#include "pch.h"
#include "MainApp/Features/AllIpcMessageLogger.h"

#include <xivres/network.h>

#include "MainApp/App.h"
#include "MainApp/Features/SocketHook.h"
#include "Misc/Logger.h"

using namespace xivres::network;

struct XivAlexander::Apps::MainApp::Features::AllIpcMessageLogger::Implementation {
	class SingleConnectionHandler {
	public:
		Implementation& Impl;
		SingleConnection& Conn;

		SingleConnectionHandler(Implementation& impl, SingleConnection& conn)
			: Impl(impl)
			, Conn(conn) {

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == message_type::Ipc && pMessage->Data.Ipc.Type == ipc_type::InterestedType) {
					const char* pszPossibleMessageType;
					switch (pMessage->Length) {
						case 0x09c:
							pszPossibleMessageType = "ActionEffect01";
							break;
						case 0x29c:
							pszPossibleMessageType = "ActionEffect08";
							break;
						case 0x4dc:
							pszPossibleMessageType = "ActionEffect16";
							break;
						case 0x71c:
							pszPossibleMessageType = "ActionEffect24";
							break;
						case 0x95c:
							pszPossibleMessageType = "ActionEffect32";
							break;
						case sizeof(message_header) + sizeof(ipc_header) + sizeof(ipcs::S2C_ActorControlSelf):
							pszPossibleMessageType = "ActorControlSelf";
							break;
						case sizeof(message_header) + sizeof(ipc_header) + sizeof(ipcs::S2C_ActorCast):
							pszPossibleMessageType = "ActorCast";
							break;
						case sizeof(message_header) + sizeof(ipc_header) + sizeof(ipcs::S2C_ActorControl):
							pszPossibleMessageType = "ActorControl";
							break;
						default:
							pszPossibleMessageType = nullptr;
					}
					Impl.Logger->Format(LogCategory::AllIpcMessageLogger, "source={:08x} current={:08x} subtype={:04x} length={:x} (S2C{}{})\n{}",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.Ipc.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "",
						pMessage->represent(true));
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == message_type::Ipc && pMessage->Data.Ipc.Type == ipc_type::InterestedType) {
					const char* pszPossibleMessageType;
					switch (pMessage->Length) {
						case 0x038:
							pszPossibleMessageType = "PositionUpdate";
							break;
						case 0x040:
							pszPossibleMessageType = "ActionRequest, C2S_ActionRequestGroundTargeted, InteractTarget";
							break;
						default:
							pszPossibleMessageType = nullptr;
					}
					Impl.Logger->Format(LogCategory::AllIpcMessageLogger, "source={:08x} current={:08x} subtype={:04x} length={:x} (C2S{}{})\n{}",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.Ipc.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "",
						pMessage->represent(true));
				}
				return true;
				});
		}

		~SingleConnectionHandler() {
			Conn.RemoveMessageHandlers(this);
		}
	};

	const std::shared_ptr<Misc::Logger> Logger;
	std::map<SingleConnection*, std::unique_ptr<SingleConnectionHandler>> Handlers;
	xivres::util::on_dtor::multi Cleanup;

	Implementation(App& app)
		: Logger(Misc::Logger::Acquire()) {
		Cleanup += app.GetSocketHook().OnSocketFound([&](SingleConnection& conn) {
			Handlers.emplace(&conn, std::make_unique<SingleConnectionHandler>(*this, conn));
			});
		Cleanup += app.GetSocketHook().OnSocketGone([&](SingleConnection& conn) {
			Handlers.erase(&conn);
			});
	}

	~Implementation() {
		Handlers.clear();
	}
};

XivAlexander::Apps::MainApp::Features::AllIpcMessageLogger::AllIpcMessageLogger(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Features::AllIpcMessageLogger::~AllIpcMessageLogger() = default;
