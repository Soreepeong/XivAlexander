#pragma once

#include "XivAlexander/XivAlexander.h"

namespace XivAlexander::LoaderApp {
	enum class LauncherType : int {
		Auto,
		Select,
		SquareEnix,
		ActozSoft,
		ShengquGames,
		UserjoyGames,
		Count_,  // for internal use only
	};

	enum class InstallMode : int {
		D3D,
		DInput8,
		Dxgi,
		Count_,  // for internal use only
	};

	class Arguments {
	public:
		argparse::ArgumentParser argp;

		Dll::LoaderAction m_action = Dll::LoaderAction::Interactive;
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

		// Passed to an elevated child so that it uses the invoking user's folders, not those of the elevating account.
		std::filesystem::path m_configDir;
		std::filesystem::path m_programDir;
		bool m_gameFilesOnly = false;

		// For tests: show the cleanup dialog over fake data, or clean up fake folders without a window.
		bool m_showCleanupDialogTest = false;
		std::string m_testCleanupItems;
		bool m_debugUpdate = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000);

		Arguments();

		void Parse();

		[[nodiscard]] std::wstring GetHelpMessage() const;

		[[nodiscard]] std::set<DWORD> GetTargetPidList() const;
	};
}
