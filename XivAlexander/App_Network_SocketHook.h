#pragma once

#include "App_Misc_Hooks.h"

namespace App {
	class App;
}

namespace App::Network {
	namespace Structures {
		struct FFXIVMessage;
	}

	class SocketHook;

	class SingleConnection {
		friend class SocketHook;

		class Internals;
		const std::unique_ptr<Internals> impl;

	public:
		SingleConnection(SocketHook* hook, SOCKET s);
		~SingleConnection();

		void AddIncomingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb);
		void AddOutgoingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb);
		void RemoveMessageHandlers(void* token);
		void ResolveAddresses();
		
		[[nodiscard]]
		SOCKET GetSocket() const;

		[[nodiscard]]
		int64_t FetchSocketLatency();
		
		Utils::NumericStatisticsTracker SocketLatency{ 10, 0 };
		Utils::NumericStatisticsTracker ApplicationLatency{ 10, 0 };
		Utils::NumericStatisticsTracker ExaggeratedNetworkLatency{ 10, INT64_MAX, 30000 };
		const Utils::NumericStatisticsTracker* GetPingLatencyTracker() const;
	};

	class SocketHook {
		class Internals;
		friend class SingleConnection;
		friend class SingleConnection::Internals;
		const std::unique_ptr<Internals> impl;

		Misc::Hooks::ImportedFunction<SOCKET, int, int, int> socket{ "socket::socket", "ws2_32.dll", "socket", 23 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const sockaddr*, int> connect{ "socket::connect", "ws2_32.dll", "connect", 4 };
		Misc::Hooks::ImportedFunction<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select{ "socket::select", "ws2_32.dll", "select", 18 };
		Misc::Hooks::ImportedFunction<int, SOCKET, char*, int, int> recv{ "socket::recv", "ws2_32.dll", "recv", 16 };
		Misc::Hooks::ImportedFunction<int, SOCKET, const char*, int, int> send{ "socket::send", "ws2_32.dll", "send", 19 };
		Misc::Hooks::ImportedFunction<int, SOCKET> closesocket{ "socket::closesocket", "ws2_32.dll", "closesocket", 3 };

	public:
		SocketHook(App* pApp);
		SocketHook(const SocketHook&) = delete;
		SocketHook(SocketHook&&) = delete;
		SocketHook& operator =(const SocketHook&) = delete;
		SocketHook& operator =(SocketHook&&) = delete;
		~SocketHook();

		static SocketHook* Instance();
		[[nodiscard]] bool IsUnloadable() const;

		void AddOnSocketFoundListener(void* token, const std::function<void(SingleConnection&)>& cb);
		void AddOnSocketGoneListener(void* token, const std::function<void(SingleConnection&)>& cb);
		void RemoveListeners(void* token);
		void ReleaseSockets();

		[[nodiscard]] std::wstring Describe() const;
	};
}
