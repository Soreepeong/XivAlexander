#include "pch.h"
#include "MainApp/Windows/MainWindow.h"

#include "resource.h"
#include "XivAlexander.h"
#include "MainApp/App.h"

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_File(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_GLOBAL_SHOW_TRAYMENU:
			for (const auto& w : All())
				if (w->Handle() == GetForegroundWindow())
					ShowContextMenu(w);
			return;

		case ID_FILE_CURRENTINFO:
			if (const auto hWnd = this->m_app.GetGameWindowHandle())
				SetForegroundWindow(hWnd);
			return;

		case ID_FILE_SHOWLOGGINGWINDOW:
			config.Ui.LogWindow.Show.Toggle();
			return;

		case ID_FILE_SHOWCONTROLWINDOW:
			config.Ui.MainWindow.Show.Toggle();
			SetForegroundWindow(m_hWnd);
			return;

		case ID_FILE_CHECKFORUPDATES:
			LaunchXivAlexLoaderWithTargetHandles({ Utils::Win32::Process::Current() },
				Dll::GetUnloadDisabledReason() ? Dll::LoaderAction::UpdateCheck : Dll::LoaderAction::Internal_Update_DependencyDllMode,
				false);
			return;

		case ID_FILE_UNLOADXIVALEXANDER:
			m_triggerUnload();
			return;

		case ID_FILE_FORCEEXITGAME:
			if (Dll::MessageBoxF(m_hWnd, MB_YESNO | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_CONFIRM_EXIT_GAME)) == IDYES) {
				RemoveTrayIcon();
				TerminateProcess(GetCurrentProcess(), 0);
			}
			return;
	}
}
