#pragma once
#include "App_Window_BaseWindow.h"

namespace App::Window {
	class Config;
	
	class Main : public BaseWindow {
		XivAlexApp* m_pApp;
		const std::function<void()> m_triggerUnload;
		const uint32_t m_uTaskbarRestartMessage;

		std::unique_ptr<Config> m_runtimeConfigEditor{ nullptr };
		std::unique_ptr<Config> m_gameConfigEditor{ nullptr };

		Utils::CallOnDestruction::Multiple m_cleanup;

		std::filesystem::path m_path;
		std::wstring m_sRegion, m_sVersion;

	public:
		Main(XivAlexApp* pApp, std::function<void()> unloadFunction);
		~Main() override;

	protected:
		LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnDestroy() override;

		void RepopulateMenu(HMENU hMenu);
		void RegisterTrayIcon();
	};
};