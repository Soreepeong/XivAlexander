#pragma once

#include "App_ConfigRepository.h"
#include "App_Window_BaseWindow.h"

namespace App {
	class XivAlexApp;
}

namespace App::Window {
	class ConfigWindow;

	class MainWindow : public BaseWindow {
		XivAlexApp* m_pApp;
		const std::function<void()> m_triggerUnload;
		const uint32_t m_uTaskbarRestartMessage;

		std::unique_ptr<ConfigWindow> m_runtimeConfigEditor{ nullptr };
		std::unique_ptr<ConfigWindow> m_gameConfigEditor{ nullptr };

		Utils::CallOnDestruction::Multiple m_cleanup;

		std::filesystem::path m_path;
		std::wstring m_sRegion, m_sVersion;

		uint64_t m_lastTrayIconLeftButtonUp = 0;

		bool m_bUseDirectX11 = INTPTR_MAX == INT64_MAX;
		bool m_bUseXivAlexander = true;
		bool m_bUseParameterObfuscation = false;
		bool m_bUseElevation;
		Sqex::Language m_gameLanguage = Sqex::Language::Unspecified;
		Sqex::Region m_gameRegion = Sqex::Region::Unspecified;
		std::vector<std::pair<std::string, std::string>> m_launchParameters;

		std::map<uint16_t, std::function<void()>> m_menuIdCallbacks;

	public:
		MainWindow(XivAlexApp* pApp, std::function<void()> unloadFunction);
		~MainWindow() override;

		void ShowContextMenu(const BaseWindow* parent = nullptr) const;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnDestroy() override;
		
		void RepopulateMenu();
		void SetMenuStates() const;
		void RegisterTrayIcon();
		void RemoveTrayIcon();

		[[nodiscard]] bool LanguageRegionModifiable() const;
		void AskRestartGame(bool onlyOnModifier = false);

		void OnCommand_Menu_File(int menuId);
		void OnCommand_Menu_Restart(int menuId);
		void OnCommand_Menu_Network(int menuId);
		void OnCommand_Menu_Modding(int menuId);
		void OnCommand_Menu_Configure(int menuId);
		void OnCommand_Menu_View(int menuId);
		void OnCommand_Menu_Help(int menuId);

		[[nodiscard]] std::vector<std::filesystem::path> ChooseFileToOpen(std::span<const COMDLG_FILTERSPEC> fileTypes, UINT nTitleResId, const std::filesystem::path& defaultPath = {}) const;
		
		void ImportFontConfig(const std::filesystem::path& path);
		void ImportExcelTransformConfig(const std::filesystem::path& path);
		void AddAdditionalGameRootDirectory(std::filesystem::path path);
		std::string InstallTTMP(const std::filesystem::path& path, const Utils::Win32::Event& cancelEvent);

		std::pair<std::filesystem::path, std::string> InstallAnyFile(const std::filesystem::path& path, const Utils::Win32::Event& cancelEvent);
		void InstallMultipleFiles(const std::vector<std::filesystem::path>& paths);
	};
}
