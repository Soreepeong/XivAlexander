#pragma once

#include "App_LoaderApp_Arguments.h"

#include <XivAlexanderCommon/Sqex.h>
#include <XivAlexanderCommon/Utils_Win32_Process.h>

namespace App::LoaderApp {
	Utils::Win32::Process OpenProcessForInformation(DWORD pid, bool errorOnAccessDenied = true);

	Utils::Win32::Process OpenProcessForManipulation(DWORD pid);

	bool TestAdminRequirementForProcessManipulation(const std::set<DWORD>& pids);

	bool RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args = L"");

	bool EnsureNoWow64Emulation();
}

template<>
std::string argparse::details::repr(XivAlexDll::LoaderAction const& val);

template<>
std::string argparse::details::repr(App::LoaderApp::LauncherType const& val);

template<>
std::string argparse::details::repr(App::LoaderApp::InstallMode const& val);

template<>
std::string argparse::details::repr(Sqex::GameReleaseRegion const& val);
