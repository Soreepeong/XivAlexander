#pragma once
#include "App_Window_BaseWindow.h"

namespace App::Window {
	class Config : public BaseWindow {
		::App::Config::BaseRepository* const m_pRepository;

		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;
		std::string m_originalConfig;

		Utils::CallOnDestruction m_callbackHandle;
		const UINT m_nTitleStringResourceId;

	public:
		Config(UINT nTitleStringResourceId, App::Config::BaseRepository* pRepository);
		~Config() override;

		void Revert();
		bool TrySave();

		[[nodiscard]]
		::App::Config::BaseRepository* GetRepository() const;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height) override;
		LRESULT OnNotify(const LPNMHDR nmhdr) override;

		void ResizeMargin();
	};
}
