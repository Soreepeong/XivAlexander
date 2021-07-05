#pragma once
namespace App::Window {
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

    protected:
		const Utils::Win32::Closeable::LoadedModule m_hShCore;
		const std::shared_ptr<Config> m_config;
        const std::shared_ptr<Misc::Logger> m_logger;
        const WNDCLASSEXW m_windowClass;
        HWND m_hWnd;

        HWND m_hWndLastFocus = nullptr;

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

        HWND GetHandle() const;
        bool IsDestroyed() const;
        virtual HACCEL GetAcceleratorTable() const;

        Utils::ListenerManager<BaseWindow, void> OnDestroyListener;

    protected:

        LRESULT RunOnUiThreadWait(const std::function<LRESULT()>&);
        bool RunOnUiThread(std::function<void()>, bool immediateIfNoWindow = true);

        double GetZoom() const;

        virtual LRESULT WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
        virtual void OnLayout(double zoom, double width, double height);
        virtual LRESULT OnNotify(const LPNMHDR nmhdr);
        virtual LRESULT OnSysCommand(WPARAM commandId, short xPos, short yPos);
        virtual void OnDestroy();
        void Destroy();
	};
}