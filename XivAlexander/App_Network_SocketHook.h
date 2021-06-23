#pragma once

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
		SingleConnection(SOCKET s);
		~SingleConnection();

		void AddIncomingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb);
		void AddOutgoingFFXIVMessageHandler(void* token, std::function<bool(Structures::FFXIVMessage*, std::vector<uint8_t>&)> cb);
		void RemoveMessageHandlers(void* token);
		void ResolveAddresses();
		
		[[nodiscard]]
		SOCKET GetSocket() const;

		[[nodiscard]] _Success_(return)
		bool GetCurrentNetworkLatency(_Out_ int64_t& latency) const;
		
		Utils::NumericStatisticsTracker NetworkLatency{ 10, 0 };
		Utils::NumericStatisticsTracker ApplicationLatency{ 10, 0 };
		Utils::NumericStatisticsTracker ExaggeratedNetworkLatency{ 10, INT64_MAX, 30000 };
	};

	class SocketHook {
		class Internals;
		const std::unique_ptr<Internals> impl;

	public:
		SocketHook(HWND hGameWnd);
		SocketHook(const SocketHook&) = delete;
		SocketHook(SocketHook&&) = delete;
		SocketHook& operator =(const SocketHook&) = delete;
		SocketHook& operator =(SocketHook&&) = delete;
		~SocketHook();

		static SocketHook* Instance();

		void AddOnSocketFoundListener(void* token, const std::function<void(SingleConnection&)>& cb);
		void AddOnSocketGoneListener(void* token, const std::function<void(SingleConnection&)>& cb);
		void RemoveListeners(void* token);
		void ReleaseSockets();

		[[nodiscard]] std::wstring Describe() const;
	};
}
