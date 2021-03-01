#pragma once
namespace App::Network {
	class TcpTableWatcher {
		class Implementation;
		friend class Implementation;

		std::unique_ptr<Implementation> const m_pImpl;
		
	public:
		TcpTableWatcher();
		~TcpTableWatcher();

		static TcpTableWatcher& GetInstance();
		static void Cleanup();

		int GetSmoothedRtt(uint32_t localAddr, uint32_t localPort, uint32_t remoteAddr, uint32_t remotePort) const;
	};
}
