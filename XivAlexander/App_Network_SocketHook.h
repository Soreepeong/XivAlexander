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
		void SendFFXIVMessage(const Structures::FFXIVMessage* pMessage);
		SOCKET GetSocket() const;
		void SetTCPDelay(bool enabled);
		void ResolveAddresses();
		void AddConnectionLatencyItem(int64_t latency);
		int64_t GetMedianConnectionLatency() const;
		int64_t GetMeanConnectionLatency() const;
		int64_t GetConnectionLatencyDeviation() const;
		void AddServerResponseDelayItem(uint64_t delay);
		int64_t GetMeanServerResponseDelay() const;
		int64_t GetMedianServerResponseDelay() const;
		int64_t GetServerResponseDelayDeviation() const;
		int64_t GetConnectionLatency() const;
	};

	class SocketHook {
		class Internals;
		const std::unique_ptr<Internals> impl;

		SocketHook(const SocketHook&) = delete;
		SocketHook(SocketHook&&) = delete;
		SocketHook& operator =(const SocketHook&) = delete;
		SocketHook& operator =(SocketHook&&) = delete;

	public:
		SocketHook(HWND hGameWnd);
		~SocketHook();

		static SocketHook* Instance();

		void AddOnSocketFoundListener(void* token, std::function<void(SingleConnection&)> cb);
		void AddOnSocketGoneListener(void* token, std::function<void(SingleConnection&)> cb);
		void RemoveListeners(void* token);
	};
}
