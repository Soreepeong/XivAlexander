#pragma once

#include "App_XivAlexApp.h"

namespace App::Window::Dialog::FramerateLockingDialog {
	void ShowModal(XivAlexApp* app, HWND hParentWindow = nullptr);
	void Show(XivAlexApp* app, HWND hParentWindow = nullptr);
}
