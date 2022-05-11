#include "pch.h"
#include "Apps/MainApp/Internal/AllIpcMessageLogger.h"

#include <XivAlexanderCommon/Sqex/Network/Structure.h>

#include "Apps/MainApp/App.h"
#include "Apps/MainApp/Internal/SocketHook.h"
#include "Misc/Logger.h"

using namespace Sqex::Network::Structure;

struct XivAlexander::Apps::MainApp::Internal::AllIpcMessageLogger::Implementation {
	class SingleConnectionHandler {
	public:
		Implementation& Impl;
		SingleConnection& Conn;

		SingleConnectionHandler(Implementation& impl, SingleConnection& conn)
			: Impl(impl)
			, Conn(conn) {

			conn.AddIncomingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
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
						case (sizeof XivMessageHeader + sizeof XivIpcHeader + sizeof XivIpcs::S2C_ActorControlSelf):
							static_assert(sizeof XivIpcs::S2C_ActorControlSelf == sizeof XivIpcs::S2C_ActorCast);
							pszPossibleMessageType = "ActorControlSelf, ActorCast";
							break;
						case (sizeof XivMessageHeader + sizeof XivIpcHeader + sizeof XivIpcs::S2C_ActorControl):
							pszPossibleMessageType = "ActorControl";
							break;
						default:
							pszPossibleMessageType = nullptr;
					}
					Impl.Logger->Format(LogCategory::AllIpcMessageLogger, "source={:08x} current={:08x} subtype={:04x} length={:x} (S2C{}{})",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.Ipc.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "");
				}
				return true;
				});
			conn.AddOutgoingFFXIVMessageHandler(this, [&](auto pMessage) {
				if (pMessage->Type == MessageType::Ipc && pMessage->Data.Ipc.Type == IpcType::InterestedType) {
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
					Impl.Logger->Format(LogCategory::AllIpcMessageLogger, "source={:08x} current={:08x} subtype={:04x} length={:x} (C2S{}{})",
						pMessage->SourceActor, pMessage->CurrentActor,
						pMessage->Data.Ipc.SubType, pMessage->Length,
						pszPossibleMessageType ? ": Possibly " : "",
						pszPossibleMessageType ? pszPossibleMessageType : "");
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
	Utils::CallOnDestruction::Multiple Cleanup;

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

XivAlexander::Apps::MainApp::Internal::AllIpcMessageLogger::AllIpcMessageLogger(App& app)
	: m_pImpl(std::make_unique<Implementation>(app)) {
}

XivAlexander::Apps::MainApp::Internal::AllIpcMessageLogger::~AllIpcMessageLogger() = default;
