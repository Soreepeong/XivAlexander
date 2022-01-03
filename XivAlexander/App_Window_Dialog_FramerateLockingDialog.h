#pragma once

#include <XivAlexanderCommon/Utils_Win32_Handle.h>

#include "App_XivAlexApp.h"

namespace App::Window::Dialog::FramerateLockingDialog {
	void ShowModal(XivAlexApp* app, HWND hParentWindow = nullptr, const Utils::Win32::Event& hCancelEvent = {});
	Utils::CallOnDestruction Show(XivAlexApp* app, HWND hParentWindow = nullptr);
}
