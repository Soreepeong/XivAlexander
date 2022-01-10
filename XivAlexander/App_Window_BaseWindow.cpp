#include "pch.h"
#include "App_Window_BaseWindow.h"

#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"
#include "DllMain.h"

HWND App::Window::BaseWindow::InternalCreateWindow(
	const WNDCLASSEXW& wndclassex,
	_In_opt_ LPCWSTR lpWindowName,
	_In_ DWORD dwStyle,
	_In_ DWORD dwExStyle,
	_In_ int X,
	_In_ int Y,
	_In_ int nWidth,
	_In_ int nHeight,
	_In_opt_ HWND hWndParent,
	_In_opt_ HMENU hMenu,
	_In_ BaseWindow* pBase) {
	WNDCLASSEXW c = wndclassex;
	if (c.lpfnWndProc)
		throw std::runtime_error("lpfnWndProc cannot be not null");

	c.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		BaseWindow* pBase;

		if (uMsg == WM_NCCREATE) {
			const auto cs = reinterpret_cast<LPCREATESTRUCTW>(lParam);
			pBase = static_cast<BaseWindow*>(cs->lpCreateParams);
			pBase->m_hWnd = hwnd;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		} else {
			pBase = reinterpret_cast<BaseWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (!pBase)
			return DefWindowProcW(hwnd, uMsg, wParam, lParam);

		return pBase->WndProc(hwnd, uMsg, wParam, lParam);
	};

	if (!RegisterClassExW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		throw std::runtime_error("RegisterClass failed");

	const auto hWnd = CreateWindowExW(dwExStyle, c.lpszClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, Dll::Module(), pBase);
	if (!hWnd) {
		UnregisterClassW(c.lpszClassName, Dll::Module());
		throw std::runtime_error("CreateWindow failed");
	}
	return hWnd;
}

std::set<const App::Window::BaseWindow*> s_allBaseWindows;

App::Window::BaseWindow::BaseWindow(const WNDCLASSEXW& wndclassex,
	_In_opt_ LPCWSTR lpWindowName,
	_In_ DWORD dwStyle,
	_In_ DWORD dwExStyle,
	_In_ int X,
	_In_ int Y,
	_In_ int nWidth,
	_In_ int nHeight,
	_In_opt_ HWND hWndParent,
	_In_opt_ HMENU hMenu)
	: m_hShCore(L"Shcore.dll", LOAD_LIBRARY_SEARCH_SYSTEM32, false)
	, m_config(Config::Acquire())
	, m_logger(Misc::Logger::Acquire())
	, m_windowClass(wndclassex)
	, m_hWnd(InternalCreateWindow(wndclassex, lpWindowName, dwStyle, dwExStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, this)) {

	s_allBaseWindows.insert(this);
	m_cleanup += [this]() { s_allBaseWindows.erase(this); };
	m_cleanup += m_config->Runtime.Language.OnChangeListener([this](const auto&) {
		this->ApplyLanguage(m_config->Runtime.GetLangId());
	});
}

App::Window::BaseWindow::~BaseWindow() {
	Destroy();
	UnregisterClassW(m_windowClass.lpszClassName, Dll::Module());
}

const std::set<const App::Window::BaseWindow*>& App::Window::BaseWindow::All() {
	return s_allBaseWindows;
}

bool App::Window::BaseWindow::IsDialogLike() const {
	return false;
}

LRESULT App::Window::BaseWindow::RunOnUiThreadWait(const std::function<LRESULT()>& fn) {
	return SendMessageW(m_hWnd, AppMessageRunOnUiThread, 0, reinterpret_cast<LPARAM>(&fn));
}

auto App::Window::BaseWindow::RunOnUiThread(std::function<void()> fn, bool immediateIfNoWindow) -> bool {
	if (!m_hWnd) {
		if (immediateIfNoWindow)
			fn();
		return immediateIfNoWindow;
	}
	const auto pfn = new std::function(std::move(fn));
	if (!PostMessageW(m_hWnd, AppMessageRunOnUiThread, 1, reinterpret_cast<LPARAM>(pfn))) {
		delete pfn;
		return false;
	}
	return true;
}

double App::Window::BaseWindow::GetZoom() const {
	try {
		const auto hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		UINT newDpiX = 96;
		UINT newDpiY = 96;

		bool fallback = false;
		if (m_hShCore) {
			const auto pGetDpiForMonitor = m_hShCore.GetProcAddress<decltype(&GetDpiForMonitor)>("GetDpiForMonitor");
			if (!pGetDpiForMonitor || FAILED(pGetDpiForMonitor(hMonitor, MONITOR_DPI_TYPE::MDT_EFFECTIVE_DPI, &newDpiX, &newDpiY)))
				fallback = true;
		}
		if (fallback) {
			MONITORINFOEXW mi{};
			mi.cbSize = static_cast<DWORD>(sizeof MONITORINFOEXW);
			GetMonitorInfoW(hMonitor, &mi);
			const auto hdc = Utils::Win32::CreatedDC(CreateDCW(L"DISPLAY", mi.szDevice, nullptr, nullptr),
				nullptr,
				L"Failed to create display \"{}\" for zoom determination purposes.", mi.szDevice);
			newDpiX = GetDeviceCaps(hdc, LOGPIXELSX);
			newDpiY = GetDeviceCaps(hdc, LOGPIXELSY);
		}
		return std::min(newDpiY, newDpiX) / 96.;
	} catch (const std::exception&) {
		// uninterested in handling errors here
		return 1.;
	}
}

void App::Window::BaseWindow::ApplyLanguage(WORD languageId) {
}

LRESULT App::Window::BaseWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case AppMessageRunOnUiThread: {
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

		case WM_CREATE: {
			const char* lastStep = "";
			try {
				try {
					IPropertyStorePtr store;
					PROPVARIANT pv{};

					lastStep = "SHGetPropertyStoreForWindow";
					if (const auto r = SHGetPropertyStoreForWindow(hwnd, IID_IPropertyStore, reinterpret_cast<void**>(&store)); FAILED(r))
						throw _com_error(r);

					lastStep = "InitPropVariantFromString";
					if (const auto r = InitPropVariantFromString(L"Soreepeong.XivAlexander", &pv); FAILED(r))
						throw _com_error(r);

					lastStep = "store->SetValue";
					if (const auto r = store->SetValue(PKEY_AppUserModel_ID, pv); FAILED(r))
						throw _com_error(r);

					PropVariantClear(&pv);
				} catch (const _com_error& e) {
					if (e.Error() != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
						throw Utils::Win32::Error(e);
					}
				}
			} catch (const Utils::Win32::Error& e) {
				m_logger->Format<LogLevel::Warning>(LogCategory::General, "Failed to set System.AppUserModel.ID for XivAlexander window at step {}: {}", lastStep, e.what());
			}
			break;
		}

		case WM_SETFOCUS:
			if (m_hWndLastFocus)
				SetFocus(m_hWndLastFocus);
			break;

		case WM_SIZE:
		case WM_DPICHANGED: {
			RECT rc;
			GetClientRect(m_hWnd, &rc);
			int resizeType;
			if (IsZoomed(hwnd))
				resizeType = SIZE_MAXIMIZED;
			else if (IsIconic(hwnd))
				resizeType = SIZE_MINIMIZED;
			else
				resizeType = SIZE_RESTORED;
			OnLayout(GetZoom(), rc.right - rc.left, rc.bottom - rc.top, resizeType);
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

void App::Window::BaseWindow::OnLayout(double zoom, double width, double height, int resizeType) {
}

LRESULT App::Window::BaseWindow::OnNotify(const LPNMHDR nmhdr) {
	return DefWindowProcW(m_hWnd, WM_NOTIFY, nmhdr->idFrom, reinterpret_cast<LPARAM>(nmhdr));
}

LRESULT App::Window::BaseWindow::OnSysCommand(WPARAM commandId, short xPos, short yPos) {
	return DefWindowProcW(m_hWnd, WM_SYSCOMMAND, commandId, MAKELPARAM(xPos, yPos));
}

void App::Window::BaseWindow::OnDestroy() {
	OnDestroyListener();

	IPropertyStorePtr store;
	PROPERTYKEY pkey{};
	if (SUCCEEDED(PSGetPropertyKeyFromName(L"System.AppUserModel.ID", &pkey))
		&& SUCCEEDED(SHGetPropertyStoreForWindow(m_hWnd, IID_IPropertyStore, reinterpret_cast<void**>(&store)))) {
		PROPVARIANT pv{};
		PropVariantInit(&pv);
		store->SetValue(pkey, pv);
	}
	m_bDestroyed = true;
}

void App::Window::BaseWindow::Destroy() {
	if (m_bDestroyed)
		return;
	m_bDestroyed = true;
	DestroyWindow(m_hWnd);
}

Utils::CallOnDestruction App::Window::BaseWindow::WithTemporaryFocus() const {
	if (IsWindowVisible(m_hWnd)) {
		SetForegroundWindow(m_hWnd);
		return {};
	}

	LONG_PTR prevExStyle = SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_LAYERED);
	SetLayeredWindowAttributes(m_hWnd, 0, 0, LWA_ALPHA);
	LONG_PTR prevStyle = SetWindowLongPtrW(m_hWnd, GWL_STYLE, WS_VISIBLE);

	SetForegroundWindow(m_hWnd);

	return {
		[this, prevStyle, prevExStyle]() {
			SetWindowLongPtrW(m_hWnd, GWL_STYLE, prevStyle);
			SetLayeredWindowAttributes(m_hWnd, 0, 255, LWA_ALPHA);
			SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE, prevExStyle);
		}
	};
}
