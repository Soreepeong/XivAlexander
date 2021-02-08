#pragma once
#include "App_Window_Base.h"

namespace App::Window {
	class TrayIcon : public Base {
		const Utils::Win32Handle<HMENU, DestroyMenu> m_hMenu;
		HANDLE const m_hExitEvent;
		HWND const m_hGameWnd;
		GUID m_guid;

	public:
		TrayIcon(HWND hGameWnd, HANDLE hExitEvent);
		virtual ~TrayIcon();

	protected:
		virtual LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		virtual void OnDestroy() override;
	};
};