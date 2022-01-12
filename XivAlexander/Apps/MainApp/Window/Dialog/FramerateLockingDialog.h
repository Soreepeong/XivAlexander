#pragma once

#include <XivAlexanderCommon/Utils/Win32/Handle.h>

#include "Apps/MainApp/App.h"

namespace XivAlexander::Apps::MainApp::Window::Dialog::FramerateLockingDialog {
	void ShowModal(Apps::MainApp::App& app, HWND hParentWindow = nullptr, const Utils::Win32::Event& hCancelEvent = {});
	Utils::CallOnDestruction Show(Apps::MainApp::App& app, HWND hParentWindow = nullptr);
}
