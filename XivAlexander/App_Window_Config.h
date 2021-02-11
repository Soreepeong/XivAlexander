#pragma once
#include "App_Window_Base.h"

namespace App::Window {
	class Config : public Base {
		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;
		std::string m_originalConfig;

		Utils::CallOnDestruction m_callbackHandle;

	public:
		static std::unique_ptr<Config> m_pConfigWindow;
		Config();
		virtual ~Config();

		void Revert();
		bool TrySave();

	protected:
		virtual LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height) override;
		virtual LRESULT OnNotify(const LPNMHDR nmhdr) override;
	};
};