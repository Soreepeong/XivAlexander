#pragma once
#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class RunLauncher {
		const Arguments& m_args;
		const std::filesystem::path m_dllPath;

	public:
		RunLauncher(const Arguments& args);

		bool SelectAndRunLauncher();

		int Run();
	};
}
