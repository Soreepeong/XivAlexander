#pragma once
#include "Apps/LoaderApp/Arguments.h"

namespace XivAlexander::LoaderApp::Actions {
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
