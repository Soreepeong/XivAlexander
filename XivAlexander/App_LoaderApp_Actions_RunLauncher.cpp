#include "pch.h"
#include "App_LoaderApp_Actions_RunLauncher.h"

#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_LoaderApp.h"
#include "App_Misc_GameInstallationDetector.h"
#include "DllMain.h"
#include "resource.h"

using namespace XivAlexDll;

App::LoaderApp::Actions::RunLauncher::RunLauncher(const Arguments& args)
	: m_args(args) {
}

bool App::LoaderApp::Actions::RunLauncher::SelectAndRunLauncher() {
	if (!m_args.m_runProgram.empty())
		return RunProgramRetryAfterElevatingSelfAsNecessary(m_args.m_runProgram, m_args.m_runProgramArgs);

	try {
		IFileOpenDialogPtr pDialog;
		DWORD dwFlags;
		static const COMDLG_FILTERSPEC fileTypes[] = {
			{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_FFXIVBOOTFILES) + 1, L"ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe"},
			{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_EXECUTABLEFILES) + 1, L"*.exe"},
			{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_ALLFILES) + 1, L"*.*"},
		};
		Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(0));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetDefaultExtension(L"exe"));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(FindStringResourceEx(Dll::Module(), IDS_TITLE_SELECT_BOOT) + 1));
		Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
		Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
		Utils::Win32::Error::ThrowIfFailed(pDialog->Show(nullptr), true);

		std::wstring fileName;
		{
			IShellItemPtr pResult;
			PWSTR pszFileName;
			Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
			Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
			fileName = pszFileName;
			CoTaskMemFree(pszFileName);
		}

		return RunProgramRetryAfterElevatingSelfAsNecessary(fileName);

	} catch (const Utils::Win32::CancelledError&) {
		return true;

	} catch (const std::exception& e) {
		Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what() ? e.what() : "Unknown");
	}
	return true;
}

int App::LoaderApp::Actions::RunLauncher::Run() {
	try {
		EnableInjectOnCreateProcess(InjectOnCreateProcessAppFlags::Use | InjectOnCreateProcessAppFlags::InjectAll);

		const auto launchers = App::Misc::GameInstallationDetector::FindInstallations();
		switch (m_args.m_launcherType) {
			case LauncherType::Auto: {
				if (launchers.empty() || !m_args.m_runProgram.empty())
					return !SelectAndRunLauncher();
				else if (launchers.size() == 1)
					return !RunProgramRetryAfterElevatingSelfAsNecessary(launchers.front().BootApp);
				else {
					for (const auto& launcher : launchers) {
						if (Dll::MessageBoxF(nullptr, MB_YESNO | MB_ICONQUESTION,
							IDS_CONFIRM_LAUNCH,
							argparse::details::repr(launcher.Region), launcher.RootPath) == IDYES)
							RunProgramRetryAfterElevatingSelfAsNecessary(launcher.BootApp, L"");
					}
				}
				return 0;
			}

			case LauncherType::Select:
				return SelectAndRunLauncher();

			case LauncherType::International: {
				for (const auto& launcher : launchers)
					if (launcher.Region == Sqex::GameReleaseRegion::International)
						return !RunProgramRetryAfterElevatingSelfAsNecessary(launcher.BootApp);
				throw std::out_of_range(nullptr);
			}

			case LauncherType::Korean: {
				for (const auto& launcher : launchers)
					if (launcher.Region == Sqex::GameReleaseRegion::Korean)
						return !RunProgramRetryAfterElevatingSelfAsNecessary(launcher.BootApp);
				throw std::out_of_range(nullptr);
			}

			case LauncherType::Chinese: {
				for (const auto& launcher : launchers)
					if (launcher.Region == Sqex::GameReleaseRegion::Chinese)
						return !RunProgramRetryAfterElevatingSelfAsNecessary(launcher.BootApp);
				throw std::out_of_range(nullptr);
			}
		}
	} catch (const std::out_of_range&) {
		if (m_args.m_action == LoaderAction::Interactive) {
			if (!m_args.m_quiet)
				return !SelectAndRunLauncher();

		} else if (!m_args.m_quiet) {
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONINFORMATION, IDS_ERROR_NOT_FOUND);
		}
		return -1;

	} catch (const std::exception& e) {
		if (!m_args.m_quiet)
			Dll::MessageBoxF(nullptr, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		return -1;
	}
	return 0;
}
