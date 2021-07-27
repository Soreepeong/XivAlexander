#pragma once
namespace App::Window {
	class Config;
	
	class BaseWindow {
		static HWND InternalCreateWindow(const WNDCLASSEXW& wndclassex,
            _In_opt_ LPCWSTR lpWindowName,
            _In_ DWORD dwStyle,
            _In_ DWORD dwExStyle,
            _In_ int X,
            _In_ int Y,
            _In_ int nWidth,
            _In_ int nHeight,
            _In_opt_ HWND hWndParent,
            _In_opt_ HMENU hMenu,
            _In_ BaseWindow* pBase);

        bool m_bDestroyed = false;
        Utils::CallOnDestruction::Multiple m_cleanup;

    protected:
		const Utils::Win32::LoadedModule m_hShCore;
		const std::shared_ptr<App::Config> m_config;
        const std::shared_ptr<Misc::Logger> m_logger;
        const WNDCLASSEXW m_windowClass;
        HWND m_hWnd;

        HWND m_hWndLastFocus = nullptr;

        Utils::Win32::Accelerator m_hAcceleratorWindow;
        Utils::Win32::Accelerator m_hAcceleratorThread;

	public:

        enum : int {
            AppMessageRunOnUiThread = WM_APP + 1001,
        	AppMessageGetClassInstance,
        };

		BaseWindow(const WNDCLASSEXW& wndclassex,
            _In_opt_ LPCWSTR lpWindowName,
            _In_ DWORD dwStyle,
            _In_ DWORD dwExStyle,
            _In_ int X,
            _In_ int Y,
            _In_ int nWidth,
            _In_ int nHeight,
            _In_opt_ HWND hWndParent,
            _In_opt_ HMENU hMenu);
		
        BaseWindow(BaseWindow&&) = delete;
        BaseWindow(const BaseWindow&) = delete;
        BaseWindow operator=(BaseWindow&&) = delete;
        BaseWindow operator=(const BaseWindow&) = delete;
		virtual ~BaseWindow();

		static const std::set<const BaseWindow*>& All();

        [[nodiscard]] HWND GetHandle() const;
        [[nodiscard]] bool IsDestroyed() const;
        [[nodiscard]] bool IsDialogLike() const;
        [[nodiscard]] HACCEL GetWindowAcceleratorTable() const;
        [[nodiscard]] HACCEL GetThreadAcceleratorTable() const;

        Utils::ListenerManager<BaseWindow, void> OnDestroyListener;

        Utils::CallOnDestruction WithTemporaryFocus() const;

    protected:

        LRESULT RunOnUiThreadWait(const std::function<LRESULT()>&);
        bool RunOnUiThread(std::function<void()>, bool immediateIfNoWindow = true);

        double GetZoom() const;

        virtual void ApplyLanguage(WORD languageId);

        virtual LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
        virtual void OnLayout(double zoom, double width, double height);
        virtual LRESULT OnNotify(const LPNMHDR nmhdr);
        virtual LRESULT OnSysCommand(WPARAM commandId, short xPos, short yPos);
        virtual void OnDestroy();
        void Destroy();
	};
}