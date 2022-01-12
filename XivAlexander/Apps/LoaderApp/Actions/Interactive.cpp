#include "pch.h"
#include "Interactive.h"

#include "Apps/LoaderApp/App.h"
#include "Apps/LoaderApp/Actions/Update.h"
#include "resource.h"
#include "XivAlexander.h"

using namespace Dll;

void XivAlexander::LoaderApp::Actions::Interactive::TaskDialogState::SelectFrom(const Misc::GameInstallationDetector::GameReleaseInfo& info) {
	GamePath = info.RootPath / "game";
	BootPath = info.BootApp;
	BootPathIsInjectable = info.BootAppDirectlyInjectable;
	PathRequiresFileOpenDialog = false;
}

XivAlexander::LoaderApp::Actions::Interactive::Interactive(const Arguments& args)
	: m_args(args) {
}

int XivAlexander::LoaderApp::Actions::Interactive::Run() {
	while (m_nextTaskDialogMode != TaskDialogMode::Break) {
		const auto pids = m_args.GetTargetPidList();
		if (pids.empty())
			m_nextTaskDialogMode = TaskDialogMode::Install;
		else
			m_state.Pid = *pids.begin();

		auto builder = Utils::Win32::TaskDialog::Builder();
		if (m_nextTaskDialogMode == TaskDialogMode::Install)
			SetupBuilderForInstallation(builder, !pids.empty());
		else
			SetupBuilderForLoadOnce(builder, pids);

		auto refreshRequested = false;
		builder.WithHyperlinkHandler(L"refresh", [&](auto&) {
			refreshRequested = true;
			return Utils::Win32::TaskDialog::HyperlinkHandleResult::HandledCloseDialog;
		});
		builder.WithHyperlinkHandler(L"update", [&](auto& dialog) {
			auto withHider = dialog.WithHiddenDialog();
			try {
				Update(m_args).CheckForUpdates(m_args.m_targetProcessHandles, true);
			} catch (const std::exception& e) {
				Dll::MessageBoxF(dialog.GetHwnd(), MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			}
			return Utils::Win32::TaskDialog::HyperlinkHandleResult::HandledKeepDialog;
		});
		builder.WithHyperlinkHandler(L"homepage", [](auto& dialog) {
			try {
				Utils::Win32::ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_HOMEPAGE) + 1, dialog.GetHwnd());
			} catch (const std::exception& e) {
				Dll::MessageBoxF(dialog.GetHwnd(), MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			}
			return Utils::Win32::TaskDialog::HyperlinkHandleResult::HandledKeepDialog;
		});
		auto dialog = builder
			.WithInstance(Dll::Module())
			.WithAllowDialogCancellation()
			.WithCanBeMinimized()
			.WithButtonCommandLinks()
			.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
			.WithMainIcon(IDI_TRAY_ICON)
			.WithFooter(IDS_LOADERAPP_INTERACTIVE_FOOTER)
			.Build();
		dialog.OnDialogConstructed = [](auto& dialog) {
			SetWindowPos(dialog.GetHwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		};
		const auto res = dialog.Show();
		if (res.Button == IDCANCEL && !refreshRequested)
			break;
	}
	return 0;
}

std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> XivAlexander::LoaderApp::Actions::Interactive::SetupBuilderForInstallation_MakeRunGameLauncherOnCommandCallback() {
	return [this](auto& dialog) {
		try {
			const auto withHider = dialog.WithHiddenDialog();
			if (m_state.PathRequiresFileOpenDialog)
				ShowSelectGameInstallationDialog();

			if (m_state.BootPath.empty())
				throw std::runtime_error("Unable to detect boot path");

			if (m_state.BootPathIsInjectable || m_state.PathRequiresFileOpenDialog) {
				const auto isIntl = (lstrcmpiW(m_state.BootPath.filename().c_str(), L"ffxivboot.exe") == 0 || lstrcmpiW(m_state.BootPath.filename().c_str(), L"ffxivboot64.exe") == 0)
					&& lstrcmpiW(m_state.BootPath.parent_path().filename().wstring().c_str(), L"boot") == 0;
				Utils::Win32::RunProgram({
					.args = std::format(L"-a {} -l select {}",
						LoaderActionToString(LoaderAction::Launcher),
						Utils::Win32::ReverseCommandLineToArgv(m_state.BootPath.wstring())),
					.wait = true,
					.elevateMode = isIntl ? Utils::Win32::RunProgramParams::NeverUnlessShellIsElevated : Utils::Win32::RunProgramParams::NoElevationIfDenied,
				});
			} else {
				Utils::Win32::ShellExecutePathOrThrow(m_state.BootPath, dialog.GetHwnd());
				Dll::MessageBoxF(nullptr, MB_ICONWARNING, IDS_LOADERAPP_INTERACTIVE_NOCHAINLOAD);
			}
		} catch (const Utils::Win32::CancelledError&) {
			// pass
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
		return Utils::Win32::TaskDialog::Handled;
	};
}

void XivAlexander::LoaderApp::Actions::Interactive::SetupBuilderForInstallation(Utils::Win32::TaskDialog::Builder& builder, bool showLoadOnce) {
	builder
		.WithMainInstruction(IDS_LOADERAPP_INTERACTIVE_INSTALL_MAININSTRUCTION)
		.WithContent(IDS_LOADERAPP_INTERACTIVE_INSTALL_CONTENT)
		.WithButton({
			.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_INSTALL_D3D,
			.Callback = SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(true, InstallMode::D3D),
		})
		.WithButton({
			.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_INSTALL_DINPUT8X64,
			.Callback = SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(true, InstallMode::DInput8x64),
		})
		.WithButton({
			.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_INSTALL_DINPUT8X86,
			.Callback = SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(true, InstallMode::DInput8x86),
		})
		.WithButton({
			.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_UNINSTALL,
			.Callback = SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(false, InstallMode::D3D),  // last parameter doesn't matter
		})
		.WithButton({
			.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_RUNGAME,
			.Callback = SetupBuilderForInstallation_MakeRunGameLauncherOnCommandCallback(),
		});

	if (showLoadOnce) {
		builder.WithButton({
			.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_LOADONCE,
			.Callback = [this](auto&) {
				m_nextTaskDialogMode = TaskDialogMode::RunOnce;
				return Utils::Win32::TaskDialog::NotHandled;
			},
		});
	}

	auto defaultRadioSet = false;
	for (const auto& gameReleaseInfo : Misc::GameInstallationDetector::FindInstallations()) {
		auto resId = IDS_INSTALLATIONSTATUS_NOTINSTALLED;
		const auto selfVersion = Utils::StringSplit<std::string>(Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr)).first, ".");
		for (const auto name : {"d3d9.dll", "d3d11.dll", "dinput8.dll"}) {
			const auto path = gameReleaseInfo.GamePath() / name;
			try {
				if (Dll::IsXivAlexanderDll(path)) {
					const auto version = Utils::StringSplit<std::string>(Utils::Win32::FormatModuleVersionString(path).first, ".");
					if (selfVersion > version)
						resId = IDS_INSTALLATIONSTATUS_OLDER;
					else if (selfVersion < version)
						resId = IDS_INSTALLATIONSTATUS_NEWER;
					else
						resId = IDS_INSTALLATIONSTATUS_INSTALLED;
				}
			} catch (...) {
			}
		}

		builder.WithRadio({
			.Text = std::format(L"{}: {}\n{}", FindStringResourceEx(Dll::Module(), resId) + 1, argparse::details::repr(gameReleaseInfo.Region), gameReleaseInfo.RootPath.wstring()),
			.Callback = [this, gameReleaseInfo](auto&) {
				m_state.SelectFrom(gameReleaseInfo);
				return Utils::Win32::TaskDialog::Handled;
			},
		});
		if (!defaultRadioSet) {
			m_state.SelectFrom(gameReleaseInfo);
			defaultRadioSet = true;
		}
	}

	builder.WithRadio({
		.Text = IDS_LOADERAPP_INTERACTIVE_INSTALL_NOTLISTEDABOVE,
		.Callback = [&](auto&) {
			m_state.PathRequiresFileOpenDialog = true;
			return Utils::Win32::TaskDialog::Handled;
		},
	});
}

std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> XivAlexander::LoaderApp::Actions::Interactive::SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(bool load) {
	return [this, load](auto& dialog) {
		auto withHider = dialog.WithHiddenDialog();
		try {
			const auto process = OpenProcessForInformation(m_state.Pid);
			auto tryElevate = false;
			try {
				void(OpenProcessForManipulation(m_state.Pid));
			} catch (const Utils::Win32::Error& e) {
				if (e.Code() == ERROR_ACCESS_DENIED && !Utils::Win32::IsUserAnAdmin()) {
					tryElevate = true;
				}
			}
			const auto exitCode = Utils::Win32::RunProgram({
				.path = Utils::Win32::Process::Current().PathOf().parent_path() / (process.IsProcess64Bits() ? Dll::XivAlexLoader64NameW : Dll::XivAlexLoader32NameW),
				.args = std::format(L"-a {} {}",
					LoaderActionToString(load ? LoaderAction::Load : LoaderAction::Unload),
					process.GetId()),
				.wait = true,
				.elevateMode = tryElevate ? Utils::Win32::RunProgramParams::NoElevationIfDenied : Utils::Win32::RunProgramParams::Normal,
				}).WaitAndGetExitCode();

			// special case: if there was only one game process running, exit loader
			if (exitCode == 0 && m_args.GetTargetPidList().size() <= 1)
				ExitProcess(0);

			withHider.Cancel();
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
		return Utils::Win32::TaskDialog::NotHandled;
	};
}

void XivAlexander::LoaderApp::Actions::Interactive::SetupBuilderForLoadOnce(Utils::Win32::TaskDialog::Builder& builder, const std::set<DWORD>& pids) {
	builder
		.WithMainInstruction(IDS_LOADERAPP_LOADONCE_MAININSTRUCTION)
		.WithContent(IDS_LOADERAPP_LOADONCE_CONTENT)
		.WithButton({
			.Text = IDS_LOADERAPP_LOADONCE_LOAD,
			.Callback = SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(true),
		})
		.WithButton({
			.Text = IDS_LOADERAPP_LOADONCE_UNLOAD,
			.Callback = SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(false),
		})
		.WithButton({
			.Text = IDS_LOADERAPP_LOADONCE_INSTALL,
			.Callback = [&](auto&) {
				m_nextTaskDialogMode = TaskDialogMode::Install;
				return Utils::Win32::TaskDialog::NotHandled;
			},
		});

	for (const auto pid : pids) {
		const auto process = OpenProcessForInformation(pid, false);
		builder.WithRadio({
			.Text = std::format(L"PID: {}\n{}", pid, process ? process.PathOf().wstring() : Utils::FromUtf8(Utils::Win32::FormatWindowsErrorMessage(ERROR_ACCESS_DENIED))),
			.Callback = [this, pid](auto&) {
				m_state.Pid = pid;
				return Utils::Win32::TaskDialog::Handled;
			},
		});
	}
}

void XivAlexander::LoaderApp::Actions::Interactive::ShowSelectGameInstallationDialog() {
	IFileOpenDialogPtr pDialog;
	DWORD dwFlags;
	static const COMDLG_FILTERSPEC fileTypes[] = {
		{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_FFXIVEXECUTABLEFILES) + 1, L"ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe; ffxiv_dx11.exe; ffxiv.exe"},
		{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_EXECUTABLEFILES) + 1, L"*.exe"},
		{FindStringResourceEx(Dll::Module(), IDS_FILTERSPEC_ALLFILES) + 1, L"*"},
	};
	Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
	Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes));
	Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(0));
	Utils::Win32::Error::ThrowIfFailed(pDialog->SetDefaultExtension(L"exe"));
	Utils::Win32::Error::ThrowIfFailed(pDialog->SetTitle(FindStringResourceEx(Dll::Module(), IDS_TITLE_SELECT_FFXIVEXECUTABLE) + 1));
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

	m_state.GamePath = fileName;
	m_state.BootPathIsInjectable = true;
	if (lstrcmpiW(m_state.GamePath.filename().wstring().c_str(), Dll::GameExecutable32NameW) == 0
		|| lstrcmpiW(m_state.GamePath.filename().wstring().c_str(), Dll::GameExecutable64NameW) == 0) {
		m_state.GamePath = m_state.GamePath.parent_path();
		if (!exists(m_state.BootPath = m_state.GamePath.parent_path() / "FFXIVBoot.exe"))
			if (!exists(m_state.BootPath = m_state.GamePath.parent_path() / "boot" / "ffxivboot.exe"))
				if (!exists(m_state.BootPath = m_state.GamePath.parent_path() / "boot" / "ffxiv_boot.exe"))
					m_state.BootPath.clear();
	} else {
		m_state.BootPath = m_state.GamePath.parent_path();
		if (!exists((m_state.GamePath = m_state.BootPath / "game") / "ffxivgame.ver"))
			if (!exists((m_state.GamePath = m_state.BootPath.parent_path() / "game") / "ffxivgame.ver"))
				if (!exists((m_state.GamePath = m_state.BootPath.parent_path().parent_path() / "game") / "ffxivgame.ver"))
					m_state.GamePath.clear();
	}
}

std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> XivAlexander::LoaderApp::Actions::Interactive::SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(bool install, InstallMode installMode) {
	return [this, install, installMode](Utils::Win32::TaskDialog& dialog) {
		try {
			auto withHider = dialog.WithHiddenDialog();
			if (m_state.PathRequiresFileOpenDialog)
				ShowSelectGameInstallationDialog();

			Utils::Win32::RunProgram({
				.args = std::format(L"-a {} --install-mode {} {}",
					LoaderActionToString(install ? LoaderAction::Install : LoaderAction::Uninstall),
					argparse::details::repr(installMode),
					Utils::Win32::ReverseCommandLineToArgv(m_state.GamePath.wstring())),
				.wait = true,
				.elevateMode = Utils::Win32::RunProgramParams::Force,
			});

			withHider.Cancel();
			return Utils::Win32::TaskDialog::NotHandled;
		} catch (const Utils::Win32::CancelledError&) {
			// pass
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
		return Utils::Win32::TaskDialog::Handled;
	};
}
