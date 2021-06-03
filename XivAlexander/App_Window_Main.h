#pragma once
#include "App_Window_Base.h"

namespace App::Window {
	class Config;
	
	class Main : public Base {
		HWND const m_hGameWnd;
		GUID m_guid{};
		const std::function<void()> m_triggerUnload;
		const int m_uTaskbarRestartMessage;

		std::unique_ptr<Config> m_runtimeConfigEditor{ nullptr };
		std::unique_ptr<Config> m_gameConfigEditor{ nullptr };

		std::vector<Utils::CallOnDestruction> m_cleanupList;

	public:
		Main(HWND hGameWnd, std::function<void()> unloadFunction);
		virtual ~Main();

	protected:
		LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnDestroy() override;

		void RepopulateMenu(HMENU hMenu);
		void RegisterTrayIcon();
	};
};