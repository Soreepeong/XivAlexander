#include "pch.h"
#include "MainApp/Windows/MainWindow.h"

#include "resource.h"

void XivAlexander::Apps::MainApp::Window::MainWindow::OnCommand_Menu_View(int menuId) {
	auto& config = m_config->Runtime;

	switch (menuId) {
		case ID_VIEW_ALWAYSONTOP:
			config.Ui.MainWindow.AlwaysOnTop.Toggle();
			return;

		case ID_VIEW_ALWAYSONTOPGAME:
			config.GameWindow.AlwaysOnTop.Toggle();
			return;

		case ID_VIEW_HIDEONMINIMIZE:
			config.Ui.MainWindow.HideOnMinimize.Toggle();
			return;
	}
}
