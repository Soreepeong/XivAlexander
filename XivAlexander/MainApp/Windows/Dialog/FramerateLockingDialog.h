#pragma once

#include "Utils/Win32/Handle.h"

#include "MainApp/App.h"

namespace XivAlexander::Apps::MainApp::Window::Dialog::FramerateLockingDialog {
	void ShowModal(App& app, HWND hParentWindow = nullptr, const Utils::Win32::Event& hCancelEvent = {});
	xivres::util::on_dtor Show(App& app, HWND hParentWindow = nullptr);
}
