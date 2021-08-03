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

		uint64_t m_lastTrayIconLeftButtonUp = 0;

		bool m_bUseDirectX11 = INTPTR_MAX == INT64_MAX;
		bool m_bUseXivAlexander = true;
		bool m_bUseParameterObfuscation = false;
		bool m_bUseElevation;
		std::vector<std::pair<std::string, std::string>> m_launchParameters;

	public:
		Main(XivAlexApp* pApp, std::function<void()> unloadFunction);
		~Main() override;

		void ShowContextMenu(const BaseWindow* parent = nullptr) const;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnDestroy() override;

		void RepopulateMenu(HMENU hMenu) const;
		void RegisterTrayIcon();
		void RemoveTrayIcon();

		void AskRestartGame();
	};
}
