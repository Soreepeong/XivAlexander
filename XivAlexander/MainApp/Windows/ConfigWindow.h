#pragma once

#include "Config.h"
#include "MainApp/Windows/BaseWindow.h"

namespace XivAlexander::Apps::MainApp::Window {
	class ConfigWindow : public BaseWindow {
		// The repository whose file is edited, or none, for a file of its own.
		BaseConfigRepository* const m_pRepository;
		const std::filesystem::path m_path;
		const std::wstring m_title;

		HWND m_hScintilla = nullptr;
		SciFnDirect m_direct = nullptr;
		sptr_t m_directPtr = 0;
		std::string m_originalConfig;

		xivres::util::on_dtor::multi m_cleanup;
		xivres::util::on_dtor m_callbackHandle;
		const UINT m_nTitleStringResourceId;

	public:
		ConfigWindow(UINT nTitleStringResourceId, BaseConfigRepository* pRepository);
		ConfigWindow(std::wstring title, std::filesystem::path path);
		~ConfigWindow() override;

		void Revert();
		bool TrySave();

		[[nodiscard]] auto Repository() const { return m_pRepository; }
		[[nodiscard]] std::filesystem::path GetPath() const;

	protected:
		void ApplyLanguage(WORD languageId) final;

		LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
		void OnLayout(double zoom, double width, double height, int resizeType) override;
		LRESULT OnNotify(LPNMHDR nmhdr) override;
		void OnThemeChanged() override;

		void Initialize();
		void ApplyScintillaTheme();
		void ResizeMargin();
	};
}
