#pragma once

#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class Update {
		const Arguments& m_args;

	public:
		Update(const Arguments& args);

		void CheckForUpdates(std::vector<Utils::Win32::Process> prevProcesses, bool offerAutomaticUpdate);
		
		int Run();

	private:
		bool RequiresElevationForUpdate(std::vector<DWORD> excludedPid);

		void PerformUpdateAndExitIfSuccessful(std::vector<Utils::Win32::Process> gameProcesses, const std::string& url, const std::filesystem::path& updateZip);

		void UpdateStep_ReplaceFiles();
		void UpdateStep_CleanupFiles();
	};
}
