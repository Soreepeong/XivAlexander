#pragma once

#include "LoaderApp/Arguments.h"

#include <xivres/common.h>
#include "Utils/Win32/Process.h"

namespace XivAlexander::LoaderApp {
	Utils::Win32::Process OpenProcessForInformation(DWORD pid, bool errorOnAccessDenied = true);

	Utils::Win32::Process OpenProcessForManipulation(DWORD pid);

	bool TestAdminRequirementForProcessManipulation(const std::set<DWORD>& pids);

	bool RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args = L"");

	bool EnsureNoWow64Emulation();

	class LoaderApp;
}

template<>
std::string argparse::details::repr(Dll::LoaderAction const& val);

template<>
std::string argparse::details::repr(XivAlexander::LoaderApp::LauncherType const& val);

template<>
std::string argparse::details::repr(XivAlexander::LoaderApp::InstallMode const& val);

template<>
std::string argparse::details::repr(xivres::game_release_publisher const& val);
