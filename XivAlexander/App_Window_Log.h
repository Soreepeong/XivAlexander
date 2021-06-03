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
		~Log() override;

	protected:
		LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height) override;
		LRESULT OnNotify(const LPNMHDR nmhdr) override;
		void OnDestroy() override;

		void ResizeMargin();
	};
};