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
		void ResolveAddresses();
		void AddConnectionLatencyItem(int64_t latency);
		void AddServerResponseDelayItem(uint64_t delay);
		
		[[nodiscard]]
		SOCKET GetSocket() const;
		[[nodiscard]]
		int64_t GetMedianConnectionLatency() const;
		[[nodiscard]]
		int64_t GetMeanConnectionLatency() const;
		[[nodiscard]]
		int64_t GetConnectionLatencyDeviation() const;
		[[nodiscard]]
		int64_t GetMinServerResponseDelay() const;
		[[nodiscard]]
		int64_t GetMeanServerResponseDelay() const;
		[[nodiscard]]
		int64_t GetMedianServerResponseDelay() const;
		[[nodiscard]]
		int64_t GetServerResponseDelayDeviation() const;
		[[nodiscard]]
		int64_t GetConnectionLatency() const;
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

		void AddOnSocketFoundListener(void* token, std::function<void(SingleConnection&)> cb);
		void AddOnSocketGoneListener(void* token, std::function<void(SingleConnection&)> cb);
		void RemoveListeners(void* token);

		std::wstring Describe() const;
	};
}
