#pragma once

namespace App {
	namespace Network {
		class SocketHook;
	}
}

namespace App::Feature {
	class AnimationLockLatencyHandler {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		AnimationLockLatencyHandler(Network::SocketHook* socketHook);
		~AnimationLockLatencyHandler();
	};
}
