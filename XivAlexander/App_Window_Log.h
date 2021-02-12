#pragma once
#include "App_Window_Base.h"

namespace App::Window {
	class Log : public Base {
		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;

		Utils::CallOnDestruction m_callbackHandle;

	public:
		Log();
		virtual ~Log();

	protected:
		virtual LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height) override;
		LRESULT OnNotify(const LPNMHDR nmhdr) override;
		LRESULT OnSysCommand(WPARAM commandId, short xPos, short yPos) override;
		virtual void OnDestroy() override;

		void ResizeMargin();
	};
};