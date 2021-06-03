#pragma once
namespace App::Window {
	class Base {
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
            _In_ Base* pBase);

        bool m_bDestroyed = false;

    protected:
        const WNDCLASSEXW m_windowClass;
        HWND m_hWnd;

        HWND m_hWndLastFocus = nullptr;

	public:

        enum : int {
            AppMessageRunOnUiThread = WM_APP + 1001,
        };

		Base(const WNDCLASSEXW& wndclassex,
            _In_opt_ LPCWSTR lpWindowName,
            _In_ DWORD dwStyle,
            _In_ DWORD dwExStyle,
            _In_ int X,
            _In_ int Y,
            _In_ int nWidth,
            _In_ int nHeight,
            _In_opt_ HWND hWndParent,
            _In_opt_ HMENU hMenu);
        Base(Base&&) = delete;
        Base(const Base&) = delete;
        Base operator =(Base&&) = delete;
        Base operator =(const Base&) = delete;
		virtual ~Base();

        HWND GetHandle() const;
        bool IsDestroyed() const;
        virtual HACCEL GetAcceleratorTable() const;

        Utils::ListenerManager<Base, void> OnDestroyListener;

        static const std::set<Base*>& GetAllOpenWindows();

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