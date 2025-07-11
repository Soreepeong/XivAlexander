﻿#pragma once

#include "Config.h"
#include "Misc/GameInstallationDetector.h"
#include "Apps/MainApp/Internal/VirtualSqPacks.h"
#include "Apps/MainApp/Window/BaseWindow.h"

namespace XivAlexander::Apps {
	class MainApp::App;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ConfigWindow;
	class ProgressPopupWindow;

	class MainWindow : public BaseWindow {
		Apps::MainApp::App& m_app;
		const std::function<void()> m_triggerUnload;
		const uint32_t m_uTaskbarRestartMessage;

		ConfigWindow* m_runtimeConfigEditor{ nullptr };
		ConfigWindow* m_gameConfigEditor{ nullptr };

		std::filesystem::path m_path;
		Misc::GameInstallationDetector::GameReleaseInfo m_gameReleaseInfo;

		uint64_t m_lastTrayIconLeftButtonUp = 0;

		bool m_bUseDirectX11 = INTPTR_MAX == INT64_MAX;
		bool m_bUseXivAlexander = true;
		bool m_bUseParameterObfuscation = false;
		bool m_bUseElevation;
		Sqex::Language m_gameLanguage = Sqex::Language::Unspecified;
		Sqex::Region m_gameRegion = Sqex::Region::Unspecified;
		const std::vector<std::pair<std::string, std::string>> m_launchParameters;
		const std::wstring m_startupArgumentsForDisplay;

		Utils::Win32::Thread m_backgroundWorkerThread;
		std::shared_ptr<ProgressPopupWindow> m_backgroundWorkerProgressWindow;

		std::map<uint16_t, std::function<void()>> m_menuIdCallbacks;

		bool m_sqpacksLoaded = false;

		Utils::CallOnDestruction::Multiple m_cleanup;
		Utils::CallOnDestruction m_cleanupFramerateLockDialog;

	public:
		MainWindow(Apps::MainApp::App& app, std::function<void()> unloadFunction);
		~MainWindow() override;

		void ShowContextMenu(const BaseWindow* parent = nullptr) const;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnDestroy() override;
		
		void RepopulateMenu();
		UINT_PTR RepopulateMenu_AllocateMenuId(std::function<void()>);
		static std::wstring RepopulateMenu_GetMenuTextById(HMENU hParentMenu, UINT commandId);
		void RepopulateMenu_Ttmp(HMENU hParentMenu, HMENU hTtmpMenu);
		void RepopulateMenu_GameFix(HMENU hParentMenu);
		void SetMenuStates() const;
		void RegisterTrayIcon();
		void RemoveTrayIcon();

		void AskRestartGame(bool onlyOnModifier = false);

		void OnCommand_Menu_File(int menuId);
		void OnCommand_Menu_Restart(int menuId);
		void OnCommand_Menu_Network(int menuId);
		void OnCommand_Menu_Modding(int menuId);
		void OnCommand_Menu_Configure(int menuId);
		void OnCommand_Menu_View(int menuId);
		void OnCommand_Menu_Help(int menuId);

		[[nodiscard]] std::vector<std::filesystem::path> ChooseFileToOpen(std::span<const COMDLG_FILTERSPEC> fileTypes, UINT nTitleResId, const std::filesystem::path& defaultPath = {}) const;

		std::string InstallTTMP(const std::filesystem::path& path, Apps::MainApp::Window::ProgressPopupWindow& progressWindow);
		void BatchTtmpOperation(Internal::VirtualSqPacks::NestedTtmp& parent, int menuId);

		std::pair<std::filesystem::path, std::string> InstallAnyFile(const std::filesystem::path& path, Apps::MainApp::Window::ProgressPopupWindow& progressWindow);
		void InstallMultipleFiles(const std::vector<std::filesystem::path>& paths);

		void EnsureAndOpenDirectory(const std::filesystem::path& path);

		void CheckUpdatedOpcodes(bool showResultMessageBox);
	};
}
