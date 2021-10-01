#pragma once
#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class Inject {
		const Arguments& m_args;
		const std::filesystem::path m_dllPath;

	public:
		Inject(const Arguments& args);

		int Run();

	private:
		void HookEntryPoint();
		void InjectOrCleanup(bool inject);
	};
}
