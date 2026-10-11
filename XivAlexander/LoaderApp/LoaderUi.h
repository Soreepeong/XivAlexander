#pragma once

#include "Utils/Win32/Closeable.h"

// Pieces shared by the loader's dialogs. The loader has no Config, so BaseWindow is not used.
namespace XivAlexander::LoaderApp::Ui {
	class Strings {
		WORD m_langId;

	public:
		explicit Strings(WORD langId) : m_langId(langId) {}

		[[nodiscard]] WORD LangId() const { return m_langId; }

		[[nodiscard]] LPCWSTR operator()(UINT id) const;

		template<typename... Args>
		[[nodiscard]] std::wstring Format(UINT id, Args&&... args) const {
			return std::vformat((*this)(id), std::make_wformat_args(args...));
		}
	};

	/// The dialog template in the given language, for CreateDialogIndirectParamW and DialogBoxIndirectParamW.
	[[nodiscard]] const DLGTEMPLATE* LoadDialogTemplate(UINT id, WORD langId);

	/// Dark mode for a dialog and its children; see HandleMessage.
	class DialogTheme {
		bool m_dark = false;
		Utils::Win32::Brush m_brush;

	public:
		[[nodiscard]] bool Dark() const { return m_dark; }

		void Apply(HWND hDlg, bool dark);

		/// WM_CTLCOLOR* and checkbox custom draw; a value to return from the dialog procedure if handled.
		std::optional<INT_PTR> HandleMessage(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) const;
	};

	void CenterOnMonitorOf(HWND hWnd, HWND hReference);

	/// Width that fits the control's text in its font, in pixels.
	[[nodiscard]] int MeasureTextWidth(HWND hControl);

	[[nodiscard]] std::wstring FormatSize(uint64_t bytes);

	/// Shows a popup menu under a button and returns the chosen command, or 0.
	[[nodiscard]] UINT TrackMenuUnder(HWND hButton, HMENU hMenu);
}
