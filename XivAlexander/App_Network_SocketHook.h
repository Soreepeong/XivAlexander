#pragma once

#include <XivAlexanderCommon/Utils_ListenerManager.h>
#include <XivAlexanderCommon/Utils_NumericStatisticsTracker.h>

#include "App_Misc_Hooks.h"

namespace App {
	namespace Misc {
		class Logger;
	}

	class XivAlexApp;
}

namespace App::Network {
	namespace Structures {
		struct FFXIVBundle;
		struct FFXIVMessage;
	}

	class SocketHook;

	class SingleConnection {
		friend class SocketHook;

		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		SingleConnection(SocketHook* hook, SOCKET s);
		~SingleConnection();

		typedef std::function<bool(Structures::FFXIVMessage*)> MessageMangler;
		void AddIncomingFFXIVMessageHandler(void* token, MessageMangler cb);
		void AddOutgoingFFXIVMessageHandler(void* token, MessageMangler cb);
		void RemoveMessageHandlers(void* token);
		void ResolveAddresses();

		[[nodiscard]] SOCKET GetSocket() const;

		[[nodiscard]] int64_t FetchSocketLatency();

		Utils::NumericStatisticsTracker SocketLatency{ 10, 0 };
		Utils::NumericStatisticsTracker ApplicationLatency{ 10, 0 };
		Utils::NumericStatisticsTracker ExaggeratedNetworkLatency{ 10, INT64_MAX, 30000 };
		const Utils::NumericStatisticsTracker* GetPingLatencyTracker() const;
	};

	class SocketHook {
		struct Implementation;
		friend class SingleConnection;
		friend struct SingleConnection::Implementation;

	public:
		std::shared_ptr<Misc::Logger> const m_logger;
		Utils::ListenerManager<Implementation, void, SingleConnection&> OnSocketFound;
		Utils::ListenerManager<Implementation, void, SingleConnection&> OnSocketGone;

	private:
		std::unique_ptr<Implementation> m_pImpl;

		Misc::Hooks::ImportedFunction<SOCKET, int, int, int> socket{ "socket::socket", "ws2_32.dll", "socket", 23 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const sockaddr*, int> connect{ "socket::connect", "ws2_32.dll", "connect", 4 };
		Misc::Hooks::ImportedFunction<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select{ "socket::select", "ws2_32.dll", "select", 18 };
		Misc::Hooks::ImportedFunction<int, SOCKET, char*, int, int> recv{ "socket::recv", "ws2_32.dll", "recv", 16 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const char*, int, int> send{ "socket::send", "ws2_32.dll", "send", 19 };
		Misc::Hooks::ImportedFunction<int, SOCKET> closesocket{ "socket::closesocket", "ws2_32.dll", "closesocket", 3 };

	public:
		SocketHook(XivAlexApp* pApp);
		SocketHook(const SocketHook&) = delete;
		SocketHook(SocketHook&&) = delete;
		SocketHook& operator=(const SocketHook&) = delete;
		SocketHook& operator=(SocketHook&&) = delete;
		~SocketHook();

		[[nodiscard]] bool IsUnloadable() const;

		void ReleaseSockets();

		[[nodiscard]] std::wstring Describe() const;
	};
}
