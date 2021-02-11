#ifndef _APP_H_
#define _APP_H_

namespace App {
	class App {

		class Internals;

		App(const App&) = delete;
		App(App&&) = delete;
		App& operator =(const App&) = delete;
		App& operator =(App&&) = delete;

	public:
		App();

		const std::unique_ptr<Internals> pInternals;

		void Run();
		int Unload();
		void QueueRunOnMessageLoop(std::function<void()>, bool wait = false);
		void AddHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal = nullptr);
	};
}

#endif
