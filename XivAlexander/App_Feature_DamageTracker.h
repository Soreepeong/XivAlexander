#pragma once

namespace App {
	namespace Network {
		class SocketHook;
	}
}

namespace App::Feature {
	class DamageTracker {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		DamageTracker(Network::SocketHook* socketHook);
		~DamageTracker();
	};
}
