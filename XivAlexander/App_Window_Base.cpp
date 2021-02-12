#include "pch.h"
#include "App_Window_Base.h"

HWND App::Window::Base::InternalCreateWindow(const WNDCLASSEXW& wndclassex, LPCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, Base* pBase) {
	WNDCLASSEXW c = wndclassex;
	if (c.lpfnWndProc)
		throw std::exception("lpfnWndProc cannot be not null");

	c.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		Base* pBase;

		if (uMsg == WM_NCCREATE) {
			const auto cs = reinterpret_cast<LPCREATESTRUCTW>(lParam);
			pBase = reinterpret_cast<Base*>(cs->lpCreateParams);
			pBase->m_hWnd = hWnd;
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		} else {
			pBase = reinterpret_cast<Base*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		}

		if (!pBase)
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		return pBase->WndProc(uMsg, wParam, lParam);
	};

	if (!RegisterClassExW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		throw std::exception("RegisterClass failed");

	const auto hWnd = CreateWindowExW(dwExStyle, c.lpszClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, g_hInstance, pBase);
	if (!hWnd) {
		UnregisterClassW(c.lpszClassName, g_hInstance);
		throw std::exception("CreateWindow failed");
	}
	return hWnd;
}

App::Window::Base::Base(const WNDCLASSEXW& wndclassex, LPCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu) 
: m_windowClass(wndclassex)
, m_hWnd(InternalCreateWindow(wndclassex, lpWindowName, dwStyle, dwExStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, this)) {
}

App::Window::Base::~Base() {
	Destroy();
	UnregisterClassW(m_windowClass.lpszClassName, g_hInstance);
}

HWND App::Window::Base::GetHandle() const {
	return m_hWnd;
}

bool App::Window::Base::IsDestroyed() const {
	return m_bDestroyed;
}

HACCEL App::Window::Base::GetAcceleratorTable() const {
	return NULL;
}

LRESULT App::Window::Base::RunOnUiThreadWait(std::function<LRESULT()> fn) {
	return SendMessage(m_hWnd, AppMessageRunOnUiThread, 0, LPARAM(&fn));
}

bool App::Window::Base::RunOnUiThread(std::function<void()> fn, bool immediateIfNoWindow) {
	if (!m_hWnd) {
		if (immediateIfNoWindow)
			fn();
		return immediateIfNoWindow;
	}
	const auto pfn = new std::function<void()>(fn);
	if (!PostMessage(m_hWnd, AppMessageRunOnUiThread, 1, LPARAM(pfn))) {
		delete pfn;
		return false;
	}
	return true;
}

double App::Window::Base::GetZoom() const {
	const auto monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	UINT newDpiX;
	UINT newDpiY;
	if (FAILED(GetDpiForMonitor(monitor, MONITOR_DPI_TYPE::MDT_EFFECTIVE_DPI, &newDpiX, &newDpiY))) {
		newDpiX = 96;
		newDpiY = 96;
	}
	return std::min(newDpiY, newDpiX) / 96.;
}

LRESULT App::Window::Base::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case AppMessageRunOnUiThread:
		{
			if (wParam) {
				const auto fn = reinterpret_cast<std::function<void()>*>(lParam);
				(*fn)();
				delete fn;
			} else {
				const auto fn = reinterpret_cast<std::function<LRESULT()>*>(lParam);
				return (*fn)();
			}
			break;
		}
		case WM_ACTIVATE:
			if (wParam == WA_INACTIVE)
				m_hWndLastFocus = GetFocus();
			break;

		case WM_SETFOCUS:
			if (m_hWndLastFocus)
				SetFocus(m_hWndLastFocus);
			break;

		case WM_SIZE:
		case WM_DPICHANGED:
		{
			RECT rc;
			GetClientRect(m_hWnd, &rc);
			OnLayout(GetZoom(), rc.right - rc.left, rc.bottom - rc.top);
			break;
		}

		case WM_NOTIFY:
			return OnNotify(reinterpret_cast<LPNMHDR>(lParam));

		case WM_SYSCOMMAND:
			return OnSysCommand(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		case WM_DESTROY:
			OnDestroy();
			break;
	}
	return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

void App::Window::Base::OnLayout(double zoom, double width, double height) {
}

LRESULT App::Window::Base::OnNotify(const LPNMHDR nmhdr) {
	return DefWindowProcW(m_hWnd, WM_NOTIFY, nmhdr->idFrom, reinterpret_cast<LPARAM>(nmhdr));
}

LRESULT App::Window::Base::OnSysCommand(WPARAM commandId, short xPos, short yPos) {
	return DefWindowProcW(m_hWnd, WM_SYSCOMMAND, commandId, MAKELPARAM(xPos, yPos));
}

void App::Window::Base::OnDestroy() {
	OnDestroyListener();
	m_bDestroyed = true;
}

void App::Window::Base::Destroy() {
	if (m_bDestroyed)
		return;
	m_bDestroyed = true;
	DestroyWindow(m_hWnd);
}
