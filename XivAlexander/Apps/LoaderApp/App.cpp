#include "pch.h"
#include "App.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Utils/Win32/InjectedModule.h>
#include <XivAlexanderCommon/Utils/Win32/Resource.h>

#include "Apps/LoaderApp/Actions/Inject.h"
#include "Apps/LoaderApp/Actions/InstallUninstall.h"
#include "Apps/LoaderApp/Actions/Interactive.h"
#include "Apps/LoaderApp/Actions/LoadUnload.h"
#include "Apps/LoaderApp/Actions/RunLauncher.h"
#include "Apps/LoaderApp/Actions/Update.h"
#include "Apps/LoaderApp/Arguments.h"
#include "resource.h"
#include "XivAlexander.h"

using namespace Dll;

Utils::Win32::Process XivAlexander::LoaderApp::OpenProcessForInformation(DWORD pid, bool errorOnAccessDenied) {
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

Utils::Win32::Process XivAlexander::LoaderApp::OpenProcessForManipulation(DWORD pid) {
	return {PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid};
}

bool XivAlexander::LoaderApp::TestAdminRequirementForProcessManipulation(const std::set<DWORD>& pids) {
	try {
		for (const auto pid : pids)
			OpenProcessForManipulation(pid);
	} catch (const Utils::Win32::Error& e) {
		if (e.Code() == ERROR_ACCESS_DENIED)
			return true;
	}
	return false;
}

bool XivAlexander::LoaderApp::RunProgramRetryAfterElevatingSelfAsNecessary(const std::filesystem::path& path, const std::wstring& args) {
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
			"-a", LoaderActionToString(Dll::LoaderAction::Launcher),
			"-l", "select",
			path.string()
		})) + (args.empty() ? L"" : L" " + args),
		.elevateMode = Utils::Win32::RunProgramParams::Force,
	});
}

bool XivAlexander::LoaderApp::EnsureNoWow64Emulation() {
	BOOL w = FALSE;
	if (!IsWow64Process(GetCurrentProcess(), &w) || !w)
		return false;

	Utils::Win32::RunProgram({
		.path = Utils::Win32::Process::Current().PathOf().parent_path() / Dll::XivAlexLoader64NameW,
		.args = Utils::Win32::SplitCommandLineIntoNameAndArgs(Dll::GetOriginalCommandLine()).second,
		.wait = true,
	});
	return true;
}

template<>
std::string argparse::details::repr(Dll::LoaderAction const& val) {
	return LoaderActionToString(val);
}

template<>
std::string argparse::details::repr(XivAlexander::LoaderApp::LauncherType const& val) {
	switch (val) {
		case XivAlexander::LoaderApp::LauncherType::Auto: return "auto";
		case XivAlexander::LoaderApp::LauncherType::Select: return "select";
		case XivAlexander::LoaderApp::LauncherType::International: return "international";
		case XivAlexander::LoaderApp::LauncherType::Korean: return "korean";
		case XivAlexander::LoaderApp::LauncherType::Chinese: return "chinese";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(XivAlexander::LoaderApp::InstallMode const& val) {
	switch (val) {
		case XivAlexander::LoaderApp::InstallMode::D3D: return "d3d";
		case XivAlexander::LoaderApp::InstallMode::DInput8x86: return "dinput8x86";
		case XivAlexander::LoaderApp::InstallMode::DInput8x64: return "dinput8x64";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(Sqex::GameReleaseRegion const& val) {
	switch (val) {
		case Sqex::GameReleaseRegion::International: return Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_CLIENT_INTERNATIONAL) + 1);
		case Sqex::GameReleaseRegion::Korean: return Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_CLIENT_KOREAN) + 1);
		case Sqex::GameReleaseRegion::Chinese: return Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_CLIENT_CHINESE) + 1);
	}
	return std::format("({})", static_cast<int>(val));
}


class XivAlexander::LoaderApp::LoaderApp {
	const Arguments& m_args;

	std::string m_errorClaimingSeDebugPrivilege;

public:
	LoaderApp(const Arguments& args)
		: m_args(args) {
		if (m_args.m_help) {
			Dll::MessageBoxF(nullptr, MB_OK, m_args.GetHelpMessage().c_str());
			ExitProcess(0);
		}

		if (const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); FAILED(hr))
			throw Utils::Win32::Error(_com_error(hr));

		try {
			Utils::Win32::AddDebugPrivilege();
		} catch (const std::exception& err) {
			m_errorClaimingSeDebugPrivilege = Utils::ToUtf8(std::vformat(FindStringResourceEx(Dll::Module(), IDS_ERROR_SEDEBUGPRIVILEGE) + 1, std::make_wformat_args(err.what())));
		}
	}

	int Run() {
		try {
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);

			const auto& currentProcess = Utils::Win32::Process::Current();
			const auto dllDir = currentProcess.PathOf().parent_path();
			const auto dllPath = dllDir / Dll::XivAlexDllNameW;

			VerifyPackageVersionOrThrow();

#ifdef _DEBUG
			Dll::MessageBoxF(nullptr, MB_OK, L"Action: {}", argparse::details::repr(m_args.m_action));
#endif
			switch (m_args.m_action) {
				case LoaderAction::Web:
					Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_MAIN) + 1);
					return 0;

				case LoaderAction::Interactive:
					// Preemptively elevate self if possible, when any of the target processes are not accessible
					if (!m_args.m_disableAutoRunAs && !Utils::Win32::IsUserAnAdmin() && TestAdminRequirementForProcessManipulation(m_args.GetTargetPidList()))
						RunElevatedSelfIfPossible();
					return Actions::Interactive(m_args).Run();

				case LoaderAction::Ask:
				case LoaderAction::Load:
				case LoaderAction::Unload:
					// Preemptively elevate self if possible, when any of the target processes are not accessible
					if (!m_args.m_disableAutoRunAs && !Utils::Win32::IsUserAnAdmin() && TestAdminRequirementForProcessManipulation(m_args.GetTargetPidList()))
						RunElevatedSelfIfPossible();
					return Actions::LoadUnload(m_args).Run();

				case LoaderAction::Launcher:
					return Actions::RunLauncher(m_args).Run();

				case LoaderAction::Install:
				case LoaderAction::Uninstall:
					return Actions::InstallUninstall(m_args).Run();

				case LoaderAction::Internal_Inject_HookEntryPoint:
				case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately:
				case LoaderAction::Internal_Inject_UnloadFromHandle:
					return Actions::Inject(m_args).Run();

				case LoaderAction::UpdateCheck:
				case LoaderAction::Internal_Update_DependencyDllMode:
				case LoaderAction::Internal_Update_Step2_ReplaceFiles:
				case LoaderAction::Internal_Update_Step3_CleanupFiles:
					return Actions::Update(m_args).Run();
			}

			throw std::logic_error("invalid m_action value");
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_UNEXPECTED,
				std::format(L"{}\nSeDebugPrivilege: {}", e.what(), m_errorClaimingSeDebugPrivilege.empty() ? "OK" : m_errorClaimingSeDebugPrivilege));

			return -1;
		}
	}

private:
	void VerifyPackageVersionOrThrow() {
		try {
			switch (CheckPackageVersion()) {
				case CheckPackageVersionResult::OK:
					break;

				case CheckPackageVersionResult::MissingFiles:
					throw std::runtime_error(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_ERROR_MISSING_FILES) + 1));

				case CheckPackageVersionResult::VersionMismatch:
					throw std::runtime_error(Utils::ToUtf8(FindStringResourceEx(Dll::Module(), IDS_ERROR_INCONSISTENT_FILES) + 1));
			}
		} catch (const std::exception& e) {
			if (Dll::MessageBoxF(nullptr, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1, IDS_ERROR_COMPONENTS, e.what()) == IDYES)
				Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_RELEASES) + 1);
			ExitProcess(-1);
		}
	}

	void RunElevatedSelfIfPossible() {
		try {
			ExitProcess(Utils::Win32::RunProgram({
				.args = std::format(L"--disable-runas {}", Utils::Win32::SplitCommandLineIntoNameAndArgs().second),
				.elevateMode = Utils::Win32::RunProgramParams::Force,
				}).WaitAndGetExitCode());
		} catch (const std::exception&) {
			// pass
		}
	}
};

extern "C" int Dll::XA_LoaderApp() {
	// SetThreadUILanguage(MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN));
	// SetThreadUILanguage(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN));

	XivAlexander::LoaderApp::Arguments parameters;
	try {
		parameters.Parse();
	} catch (const std::exception& err) {
		Dll::MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_COMMAND_LINE, err.what(), parameters.GetHelpMessage());
		return -1;
	}
	try {
		return XivAlexander::LoaderApp::LoaderApp(parameters).Run();
	} catch (const std::exception& err) {
		Dll::MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_UNEXPECTED, err.what());
		return -1;
	}
}
