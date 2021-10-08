#include "pch.h"
#include "App_LoaderApp.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "DllMain.h"
#include "resource.h"
#include "XivAlexanderCommon/Utils_Win32_Resource.h"

Utils::Win32::Process App::LoaderApp::OpenProcessForInformation(DWORD pid, bool errorOnAccessDenied) {
	try {
		return {PROCESS_QUERY_LIMITED_INFORMATION, false, pid};
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() != ERROR_ACCESS_DENIED)
			throw;
	}

	// some processes only allow PROCESS_QUERY_INFORMATION,
	// while denying PROCESS_QUERY_LIMITED_INFORMATION, so try again.
	try {
		return {PROCESS_QUERY_INFORMATION, false, pid};
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() != ERROR_ACCESS_DENIED)
			throw;
		if (errorOnAccessDenied)
			throw;
		return {};
	}
}

Utils::Win32::Process App::LoaderApp::OpenProcessForManipulation(DWORD pid) {
	return {PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid};
}

bool App::LoaderApp::TestAdminRequirementForProcessManipulation(const std::set<DWORD>& pids) {
	try {
		for (const auto pid : pids)
			OpenProcessForManipulation(pid);
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() == ERROR_ACCESS_DENIED)
			return true;
	}
	return false;
}

bool App::LoaderApp::RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args) {
	if (Utils::Win32::RunProgram({
		.path = path,
		.dir = path.parent_path().c_str(),
		.args = args,
		.elevateMode = Utils::Win32::RunProgramParams::CancelIfRequired,
	}))
		return true;

	return Utils::Win32::RunProgram({
		.dir = path.parent_path().c_str(),
		.args = Utils::FromUtf8(Utils::Win32::ReverseCommandLineToArgv({
			"--disable-runas",
			"-a", LoaderActionToString(XivAlexDll::LoaderAction::Launcher),
			"-l", "select",
			path.string()
		})) + (args.empty() ? L"" : L" " + args),
		.elevateMode = Utils::Win32::RunProgramParams::Force,
	});
}

bool App::LoaderApp::EnsureNoWow64Emulation() {
	BOOL w = FALSE;
	if (!IsWow64Process(GetCurrentProcess(), &w) || !w)
		return false;

	Utils::Win32::RunProgram({
		.path = Utils::Win32::Process::Current().PathOf().parent_path() / XivAlex::XivAlexLoader64NameW,
		.args = Utils::Win32::SplitCommandLineIntoNameAndArgs(std::wstring(Dll::GetOriginalCommandLine())).second,
		.wait = true,
	});
	return true;
}

template<>
std::string argparse::details::repr(XivAlexDll::LoaderAction const& val) {
	return LoaderActionToString(val);
}

template<>
std::string argparse::details::repr(App::LoaderApp::LauncherType const& val) {
	switch (val) {
		case App::LoaderApp::LauncherType::Auto: return "auto";
		case App::LoaderApp::LauncherType::Select: return "select";
		case App::LoaderApp::LauncherType::International: return "international";
		case App::LoaderApp::LauncherType::Korean: return "korean";
		case App::LoaderApp::LauncherType::Chinese: return "chinese";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(App::LoaderApp::InstallMode const& val) {
	switch (val) {
		case App::LoaderApp::InstallMode::D3D: return "d3d";
		case App::LoaderApp::InstallMode::DInput8x86: return "dinput8x86";
		case App::LoaderApp::InstallMode::DInput8x64: return "dinput8x64";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(Sqex::GameRegion const& val) {
	switch (val) {
		case Sqex::GameRegion::International: return Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_CLIENT_INTERNATIONAL) + 1);
		case Sqex::GameRegion::Korean: return Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_CLIENT_KOREAN) + 1);
		case Sqex::GameRegion::Chinese: return Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_CLIENT_CHINESE) + 1);
	}
	return std::format("({})", static_cast<int>(val));
}
