#pragma once

namespace App {
	namespace Network {
		class SocketHook;
	}
}

namespace App::Feature {
	class AllIpcMessageLogger {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		AllIpcMessageLogger(Network::SocketHook* socketHook);
		~AllIpcMessageLogger();
	};
}
