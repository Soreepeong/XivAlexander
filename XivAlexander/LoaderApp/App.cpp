#include "pch.h"
#include "App.h"

#include <XivAlexander/XivAlexander.h>
#include "Utils/Win32/InjectedModule.h"
#include "Utils/Win32/Resource.h"

#include "LoaderApp/Actions/Inject.h"
#include "LoaderApp/Actions/InstallUninstall.h"
#include "LoaderApp/Actions/Interactive.h"
#include "LoaderApp/Actions/LoadUnload.h"
#include "LoaderApp/Actions/RunLauncher.h"
#include "LoaderApp/Actions/Update.h"
#include "LoaderApp/Arguments.h"
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
		.args = xivres::util::unicode::convert<std::wstring>(Utils::Win32::ReverseCommandLineToArgv({
			"--disable-runas",
			"-a", LoaderActionToString(LoaderAction::Launcher),
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
		.path = Utils::Win32::Process::Current().PathOf().parent_path() / XivAlexLoader64NameW,
		.args = Utils::Win32::SplitCommandLineIntoNameAndArgs(GetOriginalCommandLine()).second,
		.wait = true,
	});
	return true;
}

template<>
std::string argparse::details::repr(LoaderAction const& val) {
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
		case XivAlexander::LoaderApp::InstallMode::DInput8: return "dinput8";
	}
	return std::format("({})", static_cast<int>(val));
}

template<>
std::string argparse::details::repr(xivres::game_release_publisher const& val) {
	switch (val) {
		case xivres::game_release_publisher::SquareEnix: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_INTERNATIONAL) + 1);
		case xivres::game_release_publisher::ActozSoft: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_KOREAN) + 1);
		case xivres::game_release_publisher::ShandaGames: return xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_CLIENT_CHINESE) + 1);
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
			MessageBoxF(nullptr, MB_OK, m_args.GetHelpMessage().c_str());
			ExitProcess(0);
		}

		if (const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); FAILED(hr))
			throw Utils::Win32::Error(_com_error(hr));

		try {
			Utils::Win32::AddDebugPrivilege();
		} catch (const std::exception& err) {
			const auto s = err.what();
			m_errorClaimingSeDebugPrivilege = xivres::util::unicode::convert<std::string>(std::vformat(FindStringResourceEx(Module(), IDS_ERROR_SEDEBUGPRIVILEGE) + 1, std::make_wformat_args(s)));
		}
	}

	int Run() {
		try {
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);

			const auto& currentProcess = Utils::Win32::Process::Current();
			const auto dllDir = currentProcess.PathOf().parent_path();
			const auto dllPath = dllDir / XivAlexDllNameW;

			VerifyPackageVersionOrThrow();

#ifdef _DEBUG
			Dll::MessageBoxF(nullptr, MB_OK, L"Action: {}", argparse::details::repr(m_args.m_action));
#endif
			switch (m_args.m_action) {
				case LoaderAction::Web:
					Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Module(), IDS_URL_MAIN) + 1);
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
			MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_UNEXPECTED,
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
					throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_ERROR_MISSING_FILES) + 1));

				case CheckPackageVersionResult::VersionMismatch:
					throw std::runtime_error(xivres::util::unicode::convert<std::string>(FindStringResourceEx(Module(), IDS_ERROR_INCONSISTENT_FILES) + 1));
			}
		} catch (const std::exception& e) {
			if (MessageBoxF(nullptr, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON1, IDS_ERROR_COMPONENTS, e.what()) == IDYES)
				Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Module(), IDS_URL_RELEASES) + 1);
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
	const auto activationContextCleanup = ActivationContext().With();

	// SetThreadUILanguage(MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN));
	// SetThreadUILanguage(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN));

	XivAlexander::LoaderApp::Arguments parameters;
	try {
		parameters.Parse();
	} catch (const std::exception& err) {
		MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_COMMAND_LINE, err.what(), parameters.GetHelpMessage());
		return -1;
	}
	try {
		return XivAlexander::LoaderApp::LoaderApp(parameters).Run();
	} catch (const std::exception& err) {
		MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_UNEXPECTED, err.what());
		return -1;
	}
}
