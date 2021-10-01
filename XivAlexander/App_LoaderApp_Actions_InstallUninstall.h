#pragma once
#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class InstallUninstall {
		const Arguments& m_args;

	public:
		InstallUninstall(const Arguments& args);

		int Run();

		void Install(const std::filesystem::path& gamePath);
		void Uninstall(const std::filesystem::path& gamePath);
	};
}
