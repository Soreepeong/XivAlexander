#ifndef _APP_H_
#define _APP_H_

namespace App {
	class App {

		class Internals;

		App(const App&) = delete;
		App(App&&) = delete;
		App& operator =(const App&) = delete;
		App& operator =(App&&) = delete;

		const std::unique_ptr<Internals> pInternals;

	public:
		App();

		void Run();
		void QueueRunOnMessageLoop(std::function<void()>, bool wait = false);
		void AddHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal = nullptr);
	};
}

#endif
