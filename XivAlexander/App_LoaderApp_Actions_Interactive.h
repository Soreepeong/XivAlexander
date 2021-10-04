#pragma once

#include <XivAlexanderCommon/Utils_Win32_TaskDialogBuilder.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class Interactive {
		const Arguments& m_args;

		enum class TaskDialogMode {
			Install,
			RunOnce,
			Break,
		};

		TaskDialogMode m_nextTaskDialogMode = TaskDialogMode::Install;

		struct TaskDialogState {
			std::filesystem::path GamePath, BootPath;
			bool BootPathIsInjectable = false;
			bool PathRequiresFileOpenDialog = true;
			DWORD Pid = 0;

			void SelectFrom(const XivAlex::GameRegionInfo& info);
		} m_state;

	public:
		Interactive(const Arguments& args);

		int Run();

	private:
		std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(bool install, InstallMode installMode);
		std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> SetupBuilderForInstallation_MakeRunGameLauncherOnCommandCallback();
		void SetupBuilderForInstallation(Utils::Win32::TaskDialog::Builder& builder, bool showLoadOnce);

		std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(bool load);
		void SetupBuilderForLoadOnce(Utils::Win32::TaskDialog::Builder& builder, const std::set<DWORD>& pids);

		void ShowSelectGameInstallationDialog();
	};
}
