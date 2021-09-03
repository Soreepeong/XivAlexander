#pragma once

namespace App {
	namespace Network {
		class SocketHook;
	}
}

namespace App::Feature {
	class EffectApplicationDelayLogger {
		struct Implementation;
		std::unique_ptr<Implementation> m_pImpl;

	public:
		EffectApplicationDelayLogger(Network::SocketHook* socketHook);
		~EffectApplicationDelayLogger();
	};
}
