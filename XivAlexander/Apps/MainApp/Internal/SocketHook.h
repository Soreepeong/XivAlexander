#pragma once

#include <XivAlexanderCommon/Utils/ListenerManager.h>
#include <XivAlexanderCommon/Utils/NumericStatisticsTracker.h>

#include "Misc/Hooks.h"

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace Sqex::Network {
	namespace Structure {
		struct XivBundle;
		struct XivMessage;
	}
}

namespace XivAlexander::Apps::MainApp::Internal {
	class SocketHook;

	class SingleConnection {
		friend class SocketHook;

		const SOCKET m_socket;

		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

		class SingleStream;

	public:
		SingleConnection(SocketHook& hook, SOCKET s);
		~SingleConnection();

		typedef std::function<bool(Sqex::Network::Structure::XivMessage*)> MessageMangler;
		void AddIncomingFFXIVMessageHandler(void* token, MessageMangler cb);
		void AddOutgoingFFXIVMessageHandler(void* token, MessageMangler cb);
		void RemoveMessageHandlers(void* token);
		void ResolveAddresses();

		[[nodiscard]] auto Socket() const { return m_socket; }

		[[nodiscard]] std::optional<int64_t> FetchSocketLatencyUs();

		Utils::NumericStatisticsTracker SocketLatencyUs{ 10, 0 };
		Utils::NumericStatisticsTracker ApplicationLatencyUs{ 10, 0 };
		const Utils::NumericStatisticsTracker* GetPingLatencyTrackerUs() const;
	};

	class SocketHook {
		struct Implementation;
		friend class SingleConnection;
		friend struct SingleConnection::Implementation;

		bool m_unloading = false;
		Utils::Win32::Thread m_hThreadSetupHook;

		std::shared_ptr<Misc::Logger> const m_logger;
		std::unique_ptr<Implementation> m_pImpl;

		Misc::Hooks::ImportedFunction<SOCKET, int, int, int> socket{ "socket::socket", "ws2_32.dll", "socket", 23 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const sockaddr*, int> connect{ "socket::connect", "ws2_32.dll", "connect", 4 };
		Misc::Hooks::ImportedFunction<int, int, struct fd_set*, struct fd_set*, struct fd_set*, const timeval*> select{ "socket::select", "ws2_32.dll", "select", 18 };
		Misc::Hooks::ImportedFunction<int, SOCKET, char*, int, int> recv{ "socket::recv", "ws2_32.dll", "recv", 16 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const char*, int, int> send{ "socket::send", "ws2_32.dll", "send", 19 };
		Misc::Hooks::ImportedFunction<int, SOCKET> closesocket{ "socket::closesocket", "ws2_32.dll", "closesocket", 3 };

	public:
		SocketHook(Apps::MainApp::App& app);
		SocketHook(const SocketHook&) = delete;
		SocketHook(SocketHook&&) = delete;
		SocketHook& operator=(const SocketHook&) = delete;
		SocketHook& operator=(SocketHook&&) = delete;
		~SocketHook();

		Utils::ListenerManager<Implementation, void, SingleConnection&> OnSocketFound;
		Utils::ListenerManager<Implementation, void, SingleConnection&> OnSocketGone;

		[[nodiscard]] bool IsUnloadable() const;

		void ReleaseSockets();

		[[nodiscard]] std::wstring Describe() const;
	};
}
