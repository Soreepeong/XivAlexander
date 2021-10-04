#pragma once
#include "App_ConfigRepository.h"
#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class InstallUninstall {
		const Arguments& m_args;

	public:
		InstallUninstall(const Arguments& args);

		int Run();

		void Install(const std::filesystem::path& gamePath, InstallMode installMode);
		void Uninstall(const std::filesystem::path& gamePath);

	private:
		static void RevertChainLoadDlls(
			const std::filesystem::path& gamePath,
			const bool& success,
			Utils::CallOnDestruction::Multiple& revert,
			Config::RuntimeRepository& config64, Config::RuntimeRepository& config32);
		static void RemoveTemporaryFiles(const std::filesystem::path& gamePath);
	};
}
