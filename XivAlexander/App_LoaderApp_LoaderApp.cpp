#include "pch.h"
#include "App_LoaderApp.h"
#include "App_LoaderApp_LoaderApp.h"

#include <XivAlexander/XivAlexander.h>
#include <XivAlexanderCommon/Utils_Win32_InjectedModule.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>
#include <XivAlexanderCommon/XivAlex.h>

#include "App_LoaderApp_Actions_Inject.h"
#include "App_LoaderApp_Actions_InstallUninstall.h"
#include "App_LoaderApp_Actions_Interactive.h"
#include "App_LoaderApp_Actions_LoadUnload.h"
#include "App_LoaderApp_Actions_RunLauncher.h"
#include "App_LoaderApp_Actions_Update.h"
#include "App_LoaderApp_Arguments.h"
#include "DllMain.h"
#include "resource.h"

using namespace XivAlexDll;

class App::LoaderApp::LoaderApp {
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
			m_errorClaimingSeDebugPrivilege = Utils::ToUtf8(std::format(FindStringResourceEx(Dll::Module(), IDS_ERROR_SEDEBUGPRIVILEGE) + 1, err.what()));
		}
	}

	int Run() {
		try {
			SetEnvironmentVariableW(L"XIVALEXANDER_DISABLE", nullptr);

			const auto& currentProcess = Utils::Win32::Process::Current();
			const auto dllDir = currentProcess.PathOf().parent_path();
			const auto dllPath = dllDir / XivAlex::XivAlexDllNameW;

			VerifyPackageVersionOrThrow();

#ifdef _DEBUG
			Dll::MessageBoxF(nullptr, MB_OK, L"Action: {}", argparse::details::repr(m_args.m_action));
#endif
			switch (m_args.m_action) {
				case LoaderAction::Web:
					ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_MAIN) + 1);
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
			if (e.what())
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
				ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_RELEASES) + 1);
			throw std::exception(nullptr);
		}
	}

	void RunElevatedSelfIfPossible() {
		try {
			ExitProcess(Utils::Win32::RunProgram({
				.args = std::format(L"--disable-runas {}", Utils::Win32::GetCommandLineWithoutProgramName()),
				.elevateMode = Utils::Win32::RunProgramParams::Force,
			}).WaitAndGetExitCode());
		} catch (const std::exception&) {
			// pass
		}
	}
};

extern "C" int XivAlexDll::XA_LoaderApp() {
	App::LoaderApp::Arguments parameters;
	try {
		parameters.Parse();
	} catch (const std::exception& err) {
		Dll::MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_COMMAND_LINE, err.what(), parameters.GetHelpMessage());
		return -1;
	}
	try {
		return App::LoaderApp::LoaderApp(parameters).Run();
	} catch (const std::exception& err) {
		Dll::MessageBoxF(nullptr, MB_ICONWARNING, IDS_ERROR_UNEXPECTED, err.what());
		return -1;
	}
}
