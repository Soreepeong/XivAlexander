#pragma once

namespace App {
	namespace Network {
		class SocketHook;
	}
}

namespace App::Feature {
	class IpcTypeFinder {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		IpcTypeFinder(Network::SocketHook* socketHook);
		~IpcTypeFinder();
	};
}
