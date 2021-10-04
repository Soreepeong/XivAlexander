#pragma once

#include "XivAlexander/XivAlexander.h"

namespace App::LoaderApp {
	enum class LauncherType : int {
		Auto,
		Select,
		International,
		Korean,
		Chinese,
		Count_,  // for internal use only
	};

	enum class InstallMode : int {
		D3D,
		DInput8x86,
		DInput8x64,
		Count_,  // for internal use only
	};

	class Arguments {
	public:
		argparse::ArgumentParser argp;

		XivAlexDll::LoaderAction m_action = XivAlexDll::LoaderAction::Interactive;
		LauncherType m_launcherType = LauncherType::Auto;
		InstallMode m_installMode = InstallMode::D3D;
		bool m_quiet = false;
		bool m_help = false;
		bool m_disableAutoRunAs = true;
		std::set<DWORD> m_targetPids{};
		std::vector<Utils::Win32::Process> m_targetProcessHandles{};
		std::set<std::wstring> m_targetSuffix{};
		std::wstring m_runProgram;
		std::wstring m_runProgramArgs;
		bool m_debugUpdate = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);

		Arguments();

		void Parse();

		[[nodiscard]] std::wstring GetHelpMessage() const;

		[[nodiscard]] std::set<DWORD> GetTargetPidList() const;
	};
}
