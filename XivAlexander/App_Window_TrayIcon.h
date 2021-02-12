#pragma once
#include "App_Window_Base.h"

namespace App::Window {
	class TrayIcon : public Base {
		HWND const m_hGameWnd;
		GUID m_guid;
		const std::function<void()> m_triggerUnload;
		const int m_uTaskbarRestartMessage;

		Utils::CallOnDestruction m_callbackHandle;

	public:
		TrayIcon(HWND hGameWnd, std::function<void()> unloadFunction);
		virtual ~TrayIcon();

	protected:
		virtual LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		virtual void OnDestroy() override;

		void RepopulateMenu(HMENU hMenu);
		void RegisterTrayIcon();
	};
};