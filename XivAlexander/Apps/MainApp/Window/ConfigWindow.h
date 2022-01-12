#pragma once

#include "Config.h"
#include "Apps/MainApp/Window/BaseWindow.h"

namespace XivAlexander::Apps::MainApp::Window {
	class ConfigWindow : public BaseWindow {
		Config::BaseRepository* const m_pRepository;

		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;
		std::string m_originalConfig;

		Utils::CallOnDestruction m_callbackHandle;
		const UINT m_nTitleStringResourceId;

	public:
		ConfigWindow(UINT nTitleStringResourceId, Config::BaseRepository* pRepository);
		~ConfigWindow() override;

		void Revert();
		bool TrySave();

		[[nodiscard]] auto Repository() const { return m_pRepository; }

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height, int resizeType) override;
		LRESULT OnNotify(LPNMHDR nmhdr) override;

		void ResizeMargin();
	};
}
