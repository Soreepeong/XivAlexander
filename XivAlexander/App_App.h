#pragma once

#include <memory>

namespace App {
	class App {
		class Implementation;
		friend class Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

		static bool s_bUnloadDisabled;
		static App* s_pInstance;

	public:
		App();
		~App();

		void Run();
		void QueueRunOnMessageLoop(std::function<void()> f, bool wait = false);
		int Unload();

		void CheckUpdates(bool silent = true);

		static void SetDisableUnloading(bool);
		static App* Instance();
	};
}