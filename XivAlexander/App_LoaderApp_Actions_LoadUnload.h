#pragma once

#include "XivAlexanderCommon/XivAlex.h"
#include "App_LoaderApp_Arguments.h"

namespace App::LoaderApp::Actions {
	class LoadUnload {
		const Arguments& m_args;
		const std::filesystem::path m_dllPath;

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
