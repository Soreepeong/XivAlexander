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
		void ResolveAddresses();
		void AddServerResponseDelayItem(uint64_t delay);
		std::string FormatMedianServerResponseDelayStatistics() const;
		int64_t GetMedianServerResponseDelay() const;
		int64_t GetMedianLatency() const;
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
