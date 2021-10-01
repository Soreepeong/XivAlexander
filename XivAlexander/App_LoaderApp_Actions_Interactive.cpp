#include "pch.h"
#include "App_LoaderApp_Actions_Interactive.h"

#include "App_LoaderApp.h"
#include "App_LoaderApp_Actions_LoadUnload.h"
#include "App_LoaderApp_Actions_Update.h"
#include "DllMain.h"
#include "resource.h"

using namespace XivAlexDll;

void App::LoaderApp::Actions::Interactive::TaskDialogState::SelectFrom(const XivAlex::GameRegionInfo& info) {
	GamePath = info.RootPath / "game";
	BootPath = info.BootApp;
	BootPathIsInjectable = info.BootAppDirectlyInjectable;
	PathRequiresFileOpenDialog = false;
}

App::LoaderApp::Actions::Interactive::Interactive(const Arguments& args)
	: m_args(args) {
}

int App::LoaderApp::Actions::Interactive::Run() {
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
				ShellExecutePathOrThrow(FindStringResourceEx(Dll::Module(), IDS_URL_HOMEPAGE) + 1, dialog.GetHwnd());
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
			.WithFooter(LR"(<a href="refresh">Refresh</a> | <a href="update">Check for updates</a> | <a href="homepage">Homepage</a>)")
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

std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> App::LoaderApp::Actions::Interactive::SetupBuilderForInstallation_MakeRunGameLauncherOnCommandCallback() {
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
				ShellExecutePathOrThrow(m_state.BootPath, dialog.GetHwnd());
				Dll::MessageBoxF(nullptr, MB_ICONWARNING, L"Cannot launch game with XivAlexander enabled. You will have to install XivAlexander, or load XivAlexander after you have started the game and pressed the Refresh link below.");
			}
		} catch (const Utils::Win32::CancelledError&) {
			// pass
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
		return Utils::Win32::TaskDialog::Handled;
	};
}

void App::LoaderApp::Actions::Interactive::SetupBuilderForInstallation(Utils::Win32::TaskDialog::Builder& builder, bool showLoadOnce) {
	builder
		.WithMainInstruction(L"Install XivAlexander")
		.WithContent(L"You can install XivAlexander to launch XivAlexander with the game.\n\n* Requires Administrator permissions.\n* Compatible with Reshade.\n* No game data files are touched.")
		.WithButton({
			.Text = L"&Install XivAlexander\nInstall XivAlexander for the selected game installation.",
			.Callback = SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(true),
		})
		.WithButton({
			.Text = L"&Uninstall XivAlexander\nUninstall XivAlexander from the selected game installation.",
			.Callback = SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(false),
		})
		.WithButton({
			.Text = L"&Run game\nRun game launcher and load XivAlexander when game has started.",
			.Callback = SetupBuilderForInstallation_MakeRunGameLauncherOnCommandCallback(),
		});

	if (showLoadOnce) {
		builder.WithButton({
			.Text = L"&Load once...\nLoad XivAlexander into running game once without installation.",
			.Callback = [this](auto&) {
				m_nextTaskDialogMode = TaskDialogMode::RunOnce;
				return Utils::Win32::TaskDialog::NotHandled;
			},
		});
	}

	auto defaultRadioSet = false;
	for (const auto& [region, info] : XivAlex::FindGameLaunchers()) {
		auto resId = IDS_INSTALLATIONSTATUS_NOTINSTALLED;
		const auto selfVersion = Utils::StringSplit<std::string>(Utils::Win32::FormatModuleVersionString(GetModuleHandleW(nullptr)).first, ".");
		for (const auto name : {"d3d9.dll", "d3d11.dll"}) {
			const auto path = info.RootPath / "game" / name;
			try {
				if (XivAlex::IsXivAlexanderDll(path)) {
					const auto version = Utils::StringSplit<std::string>(Utils::Win32::FormatModuleVersionString(path).first, ".");
					if (selfVersion > version)
						resId = IDS_INSTALLATIONSTATUS_NEWER;
					else if (selfVersion < version)
						resId = IDS_INSTALLATIONSTATUS_OLDER;
					else
						resId = IDS_INSTALLATIONSTATUS_INSTALLED;
				}
			} catch (...) {
			}
		}

		builder.WithRadio({
			.Text = std::format(L"{}: {}\n{}", FindStringResourceEx(Dll::Module(), resId) + 1, argparse::details::repr(region), info.RootPath.wstring()),
			.Callback = [this, info](auto&) {
				m_state.SelectFrom(info);
				return Utils::Win32::TaskDialog::Handled;
			},
		});
		if (!defaultRadioSet) {
			m_state.SelectFrom(info);
			defaultRadioSet = true;
		}
	}

	builder.WithRadio({
		.Text = L"Game installation is not listed above",
		.Callback = [&](auto&) {
			m_state.PathRequiresFileOpenDialog = true;
			return Utils::Win32::TaskDialog::Handled;
		},
	});
}

std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> App::LoaderApp::Actions::Interactive::SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(bool load) {
	return [this, load](auto& dialog) {
		auto withHider = dialog.WithHiddenDialog();
		try {
			if (load)
				LoadUnload(m_args).Load(m_state.Pid);
			else
				LoadUnload(m_args).Unload(m_state.Pid);
			withHider.Cancel();
		} catch (const std::exception& e) {
			Dll::MessageBoxF(nullptr, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
		return Utils::Win32::TaskDialog::NotHandled;
	};
}

void App::LoaderApp::Actions::Interactive::SetupBuilderForLoadOnce(Utils::Win32::TaskDialog::Builder& builder, const std::set<DWORD>& pids) {
	builder
		.WithMainInstruction(L"Run XivAlexander once")
		.WithContent(L"You can run XivAlexander right now without installation.\n\n* Restart using XivAlexander menu after loading to enable modding.")
		.WithButton({
			.Text = L"&Load XivAlexander\nLoad XivAlexander immediately to the selected process.",
			.Callback = SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(true),
		})
		.WithButton({
			.Text = L"&Unload XivAlexander\nAttempt to unload XivAlexander from the selected process.",
			.Callback = SetupBuilderForLoadOnce_MakeLoadUnloadOnCommandCallback(false),
		})
		.WithButton({
			.Text = L"&Install...\nInstall XivAlexander for automatic loading when the game starts up.",
			.Callback = [&](auto&) {
				m_nextTaskDialogMode = TaskDialogMode::Install;
				return Utils::Win32::TaskDialog::NotHandled;
			},
		});

	for (const auto pid : pids) {
		const auto process = OpenProcessForInformation(pid, false);
		builder.WithRadio({
			.Text = std::format(L"Process ID: {}\n{}", pid, process ? process.PathOf().wstring() : Utils::FromUtf8(Utils::Win32::FormatWindowsErrorMessage(ERROR_ACCESS_DENIED))),
			.Callback = [this, pid](auto&) {
				m_state.Pid = pid;
				return Utils::Win32::TaskDialog::Handled;
			},
		});
	}
}

void App::LoaderApp::Actions::Interactive::ShowSelectGameInstallationDialog() {
	IFileOpenDialogPtr pDialog;
	DWORD dwFlags;
	static const COMDLG_FILTERSPEC fileTypes[] = {
		{L"FFXIV executable files (ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe; ffxiv_dx11.exe; ffxiv.exe)", L"ffxivboot.exe; ffxivboot64.exe; ffxiv_boot.exe; ffxiv_dx11.exe; ffxiv.exe"},
		{L"Executable Files (*.exe)", L"*.exe"},
		{L"All files (*.*)", L"*"},
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

	m_state.GamePath = fileName;
	m_state.BootPathIsInjectable = true;
	if (lstrcmpiW(m_state.GamePath.filename().wstring().c_str(), XivAlex::GameExecutable32NameW) == 0
		|| lstrcmpiW(m_state.GamePath.filename().wstring().c_str(), XivAlex::GameExecutable64NameW) == 0) {
		m_state.GamePath = m_state.GamePath.parent_path();
		if (!exists(m_state.BootPath = m_state.GamePath.parent_path() / "FFXIVBoot.exe"))
			if (!exists(m_state.BootPath = m_state.GamePath.parent_path() / "boot" / "ffxivboot.exe"))
				if (!exists(m_state.BootPath = m_state.GamePath.parent_path() / "boot" / "ffxiv_boot.exe"))
					m_state.BootPath.clear();
	} else {
		m_state.BootPath = m_state.GamePath;
		if (!exists((m_state.GamePath = m_state.BootPath / "game") / "ffxivgame.ver"))
			if (!exists((m_state.GamePath = m_state.BootPath.parent_path() / "game") / "ffxivgame.ver"))
				if (!exists((m_state.GamePath = m_state.BootPath.parent_path().parent_path() / "game") / "ffxivgame.ver"))
					m_state.GamePath.clear();
	}
}

std::function<Utils::Win32::TaskDialog::ActionHandled(Utils::Win32::TaskDialog&)> App::LoaderApp::Actions::Interactive::SetupBuilderForInstallation_MakeInstallUninstallOnCommandCallback(bool install) {
	return [this, install](Utils::Win32::TaskDialog& dialog) {
		try {
			auto withHider = dialog.WithHiddenDialog();
			if (m_state.PathRequiresFileOpenDialog)
				ShowSelectGameInstallationDialog();

			Utils::Win32::RunProgram({
				.args = std::format(L"-a {} {}",
					LoaderActionToString(install ? LoaderAction::Install : LoaderAction::Uninstall),
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
