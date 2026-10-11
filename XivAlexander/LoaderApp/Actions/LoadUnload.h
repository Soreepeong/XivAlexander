#pragma once

#include "LoaderApp/Arguments.h"

namespace XivAlexander::LoaderApp::Actions {
	class LoadUnload {
		const Arguments& m_args;
		const std::filesystem::path m_loaderDllPath;  // what gets loaded into the game; it loads XivAlexander64.dll
		const std::filesystem::path m_featureDllPath;

	public:
		LoadUnload(const Arguments& args);

		int Run();
		void RunForPid(DWORD pid);

		bool IsXivAlexanderLoaded(const Utils::Win32::Process& process);

		void Load(DWORD pid);
		void Unload(DWORD pid);

	private:
		void LoadOrUnload(const Utils::Win32::Process& process, bool load);
	};
}
