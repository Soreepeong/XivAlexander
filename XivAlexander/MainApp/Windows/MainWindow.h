#pragma once

#include "Config.h"
#include "Misc/GameInstallationDetector.h"
#include "MainApp/Modding/NestedTtmp.h"
#include "MainApp/Windows/BaseWindow.h"

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ConfigWindow;
	class ProgressPopupWindow;

	class MainWindow : public BaseWindow {
		App& m_app;
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
		xivres::game_language m_gameLanguage = xivres::game_language::Unspecified;
		xivres::game_publisher m_gameRegion = xivres::game_publisher::Unspecified;
		const std::vector<std::pair<std::string, std::string>> m_launchParameters;
		const std::wstring m_startupArgumentsForDisplay;

		Utils::Win32::Thread m_backgroundWorkerThread;
		std::shared_ptr<ProgressPopupWindow> m_backgroundWorkerProgressWindow;

		std::map<uint16_t, std::function<void()>> m_menuIdCallbacks;

		bool m_sqpacksLoaded = false;

		xivres::util::on_dtor::multi m_cleanup;
		xivres::util::on_dtor m_cleanupFramerateLockDialog;

	public:
		MainWindow(App& app, std::function<void()> unloadFunction);
		~MainWindow() override;

		void ShowContextMenu(const BaseWindow* parent = nullptr) const;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnDestroy() override;
		void OnThemeChanged() override;
		
		void RepopulateMenu();
		UINT_PTR RepopulateMenu_AllocateMenuId(std::function<void()>);
		static std::wstring RepopulateMenu_GetMenuTextById(HMENU hParentMenu, UINT commandId);
		void RepopulateMenu_Ttmp(HMENU hInnerTtmpMenu, HMENU hOuterTtmpMenu);
		void RepopulateMenu_TtmpChoicesProfiles(HMENU hTtmpMenu);
		void RepopulateMenu_TtmpEnable(HMENU hParentMenu, Features::Modding::NestedTtmp& nestedTtmp, const std::wstring& label);
		void RepopulateMenu_GameFix(HMENU hParentMenu);
		void RepopulateMenu_AudioResampler(HMENU hMenu);
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

		std::string InstallTTMP(const std::filesystem::path& path, ProgressPopupWindow& progressWindow);
		void BatchTtmpOperation(Features::Modding::NestedTtmp& parent, int menuId);

		std::pair<std::filesystem::path, std::string> InstallAnyFile(const std::filesystem::path& path, ProgressPopupWindow& progressWindow);
		void InstallMultipleFiles(const std::vector<std::filesystem::path>& paths);

		void EnsureAndOpenDirectory(const std::filesystem::path& path);

		void CheckUpdatedOpcodes(bool showResultMessageBox);
	};
}
