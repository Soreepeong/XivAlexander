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
	class SettingsView;
	class ProgressPopupWindow;

	class MainWindow : public BaseWindow {
		App& m_app;
		const std::function<void()> m_triggerUnload;
		const uint32_t m_uTaskbarRestartMessage;

		std::unique_ptr<ConfigWindow> m_runtimeConfigEditor;
		std::unique_ptr<ConfigWindow> m_gameConfigEditor;
		std::unique_ptr<SettingsView> m_settingsView;

		std::filesystem::path m_path;
		Misc::GameInstallationDetector::GameReleaseInfo m_gameReleaseInfo;

		uint64_t m_lastTrayIconLeftButtonUp = 0;
		DWORD m_copiedLaunchCommandLineClipboardSequence = 0;

		bool m_bUseXivAlexander = true;
		bool m_bUseParameterObfuscation = false;
		bool m_bUseElevation;
		xivres::game_language m_gameLanguage = xivres::game_language::Unspecified;
		xivres::game_publisher m_gameRegion = xivres::game_publisher::Unspecified;
		const std::vector<std::pair<std::string, std::string>> m_launchParameters;
		const std::wstring m_startupArgumentsForDisplay;

		Utils::Win32::Thread m_backgroundWorkerThread;
		std::shared_ptr<ProgressPopupWindow> m_backgroundWorkerProgressWindow;
		std::atomic<bool> m_ttmpRescanPending = false;  // Asked for while the worker was busy; done when it's done.

		std::map<uint16_t, std::function<void()>> m_menuIdCallbacks;


		xivres::util::on_dtor::multi m_cleanup;
		xivres::util::on_dtor m_cleanupFramerateLockDialog;

	public:
		MainWindow(App& app, std::function<void()> unloadFunction);
		~MainWindow() override;

		void ShowContextMenu(const BaseWindow* parent = nullptr) const;

		[[nodiscard]] bool IsDialogLike() const override;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height, int resizeType) override;
		void OnDestroy() override;
		void OnThemeChanged() override;

		void PaintStatus(HDC hdc, const RECT& rect) const;
		
		void RepopulateMenu();
		UINT_PTR RepopulateMenu_AllocateMenuId(std::function<void()>);
		static std::wstring RepopulateMenu_GetMenuTextById(HMENU hParentMenu, UINT commandId);
		[[nodiscard]] std::pair<bool, bool> GetRestartChoiceState(UINT commandId) const;
		bool ChooseForRestart(UINT commandId);
		std::vector<std::filesystem::path> ResolveDirectories(const std::vector<std::filesystem::path>& configured) const;
		std::filesystem::path ResolvePrimaryDirectory(const std::vector<std::filesystem::path>& configured, const wchar_t* defaultName) const;
		void SetMenuStates() const;
		void RegisterTrayIcon();
		void RemoveTrayIcon();

		[[nodiscard]] static std::filesystem::path GameExecutablePath();
		[[nodiscard]] std::wstring MakeLaunchArguments() const;
		void CopyLaunchCommandLine();
		void ClearCopiedLaunchCommandLine();
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
