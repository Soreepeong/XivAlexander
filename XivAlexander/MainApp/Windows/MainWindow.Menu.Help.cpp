#include "pch.h"
#include "MainApp/Windows/MainWindow.h"

#include "resource.h"

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_Help(int menuId) {
	switch (menuId) {
		case ID_HELP_OPENHELPWEBPAGE:
		case ID_HELP_OPENHOMEPAGE: {
			SHELLEXECUTEINFOW shex{
				.cbSize = sizeof shex,
				.hwnd = m_hWnd,
				.lpFile = m_config->Runtime.GetStringRes(menuId == ID_HELP_OPENHELPWEBPAGE ? IDS_URL_HELP : IDS_URL_HOMEPAGE),
				.nShow = SW_SHOW,
			};
			if (!ShellExecuteExW(&shex))
				throw Utils::Win32::Error("ShellExecuteW");
			return;
		}
	}
}
