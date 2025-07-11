﻿#pragma once

#include <XivAlexanderCommon/Utils/Win32/Handle.h>

#include "Apps/MainApp/Window/BaseWindow.h"

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow : public BaseWindow {
		HWND const m_hParentWindow;
		UINT const m_wmTaskbarButtonCreated;
		HWND m_hMessage = nullptr;
		HWND m_hProgressBar = nullptr;
		HWND m_hCancelButton = nullptr;
		HFONT m_hFont = nullptr;

		const Utils::Win32::Event m_hCancelEvent;

		ITaskbarList3Ptr m_taskBarList3;

	public:
		ProgressPopupWindow(HWND hParentWindow);
		~ProgressPopupWindow() override;

		void UpdateProgress(uint64_t progress, uint64_t max);
		void UpdateMessage(const std::string& message);
		void UpdateMessage(const std::wstring& message);

		void Cancel();
		[[nodiscard]] const Utils::Win32::Event& GetCancelEvent() const;
		DWORD DoModalLoop(int ms, std::vector<HANDLE> events = {});

		void Show();
		void Show(std::chrono::milliseconds delay);

	protected:
		void OnLayout(double zoom, double width, double height, int resizeType) override;
		void OnDestroy() override;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

		void ApplyLanguage(WORD languageId) override;
	};
}
