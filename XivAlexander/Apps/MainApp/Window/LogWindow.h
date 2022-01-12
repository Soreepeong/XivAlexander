#pragma once

#include <XivAlexanderCommon/Utils/CallOnDestruction.h>

#include "Apps/MainApp/Window/BaseWindow.h"

namespace XivAlexander {
	enum class LogLevel;
}

namespace XivAlexander::Apps::MainApp::Window {
	class LogWindow : public BaseWindow {
		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;

		Utils::CallOnDestruction::Multiple m_cleanup;
		uint64_t m_lastDisplayedLogId = 0;

	public:
		LogWindow();
		~LogWindow() override;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height, int resizeType) override;
		LRESULT OnNotify(const LPNMHDR nmhdr) override;
		void OnDestroy() override;

		void ResizeMargin();

		void FlushLog(const std::string& logstr, LogLevel level);
	};
}
