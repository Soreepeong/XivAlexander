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
		struct XivBundle;
		struct XivMessage;
	}

	class SocketHook;

	class SingleConnection {
		friend class SocketHook;

		const SOCKET m_socket;

		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		SingleConnection(SocketHook* hook, SOCKET s);
		~SingleConnection();

		typedef std::function<bool(Structures::XivMessage*)> MessageMangler;
		void AddIncomingFFXIVMessageHandler(void* token, MessageMangler cb);
		void AddOutgoingFFXIVMessageHandler(void* token, MessageMangler cb);
		void RemoveMessageHandlers(void* token);
		void ResolveAddresses();

		[[nodiscard]] auto Socket() const { return m_socket; }

		[[nodiscard]] int64_t FetchSocketLatencyUs();

		Utils::NumericStatisticsTracker SocketLatencyUs{ 10, 0 };
		Utils::NumericStatisticsTracker ApplicationLatencyUs{ 10, 0 };
		Utils::NumericStatisticsTracker ExaggeratedNetworkLatencyUs{ 10, INT64_MAX, 30000 };
		const Utils::NumericStatisticsTracker* GetPingLatencyTrackerUs() const;
	};

	class SocketHook {
		struct Implementation;
		friend class SingleConnection;
		friend struct SingleConnection::Implementation;

		bool m_unloading = false;
		Utils::Win32::Thread m_hThreadSetupHook;

	public:
		std::shared_ptr<Misc::Logger> const m_logger;
		Utils::ListenerManager<Implementation, void, SingleConnection&> OnSocketFound;
		Utils::ListenerManager<Implementation, void, SingleConnection&> OnSocketGone;

	private:
		std::unique_ptr<Implementation> m_pImpl;

		Misc::Hooks::ImportedFunction<SOCKET, int, int, int> socket{ "socket::socket", "ws2_32.dll", "socket", 23 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const sockaddr*, int> connect{ "socket::connect", "ws2_32.dll", "connect", 4 };
		Misc::Hooks::ImportedFunction<int, int, struct fd_set*, struct fd_set*, struct fd_set*, const timeval*> select{ "socket::select", "ws2_32.dll", "select", 18 };
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
		[[nodiscard]] int64_t GetLastSocketSelectCounterUs() const;

		void ReleaseSockets();

		[[nodiscard]] std::wstring Describe() const;
	};
}
