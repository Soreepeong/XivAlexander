#pragma once

#include "App_LoaderApp_Arguments.h"

#include <XivAlexanderCommon/Utils_Win32_Process.h>
#include <XivAlexanderCommon/XivAlex.h>

namespace App::LoaderApp {
	Utils::Win32::Process OpenProcessForInformation(DWORD pid, bool errorOnAccessDenied = true);

	Utils::Win32::Process OpenProcessForManipulation(DWORD pid);

	bool TestAdminRequirementForProcessManipulation(const std::set<DWORD>& pids);

	bool RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args = L"");

	bool EnsureNoWow64Emulation();

	void ShellExecutePathOrThrow(const std::filesystem::path& path, HWND hwndOwner = nullptr);

}

template<>
std::string argparse::details::repr(XivAlexDll::LoaderAction const& val);

template<>
std::string argparse::details::repr(App::LoaderApp::LauncherType const& val);

template<>
std::string argparse::details::repr(App::LoaderApp::InstallMode const& val);

template<>
std::string argparse::details::repr(XivAlex::GameRegion const& val);
