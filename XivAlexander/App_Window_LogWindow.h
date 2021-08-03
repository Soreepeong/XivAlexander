#pragma once
#include "App_Window_BaseWindow.h"

namespace App::Window {
	class Log : public BaseWindow {
		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;

		Utils::CallOnDestruction::Multiple m_cleanup;

	public:
		Log();
		~Log() override;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height) override;
		LRESULT OnNotify(const LPNMHDR nmhdr) override;
		void OnDestroy() override;

		void ResizeMargin();

		void FlushLog(const std::string& logstr, LogLevel level);
	};
};