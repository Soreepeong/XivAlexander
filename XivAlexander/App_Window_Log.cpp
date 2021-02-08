#include "pch.h"
#include "App_Window_Log.h"

constexpr auto TimeStyle = static_cast<int>(App::Misc::Logger::LogLevel::Debug) - 1;

constexpr int BaseFontSize = 8;

enum SysMenuExtras {
	SystemMenuClear = 0xE000,
	SystemMenuAlwaysOnTop,
	SystemMenuEndSeparator,
};

static WNDCLASSEXW WindowClass() {
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = L"XivAlexander::Window::Log";
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);
	return wcex;
}

App::Window::Log::Log()
	: Base(WindowClass(), L"Log", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr) {

	NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

	m_hScintilla = CreateWindowExW(0, TEXT("Scintilla"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 0, 0, 0, m_hWnd, nullptr, g_hInstance, nullptr);
	m_direct = reinterpret_cast<SciFnDirect>(SendMessageW(m_hScintilla, SCI_GETDIRECTFUNCTION, 0, 0));
	m_directPtr = static_cast<sptr_t>(SendMessageW(m_hScintilla, SCI_GETDIRECTPOINTER, 0, 0));
	m_direct(m_directPtr, SCI_STYLESETSIZE, STYLE_DEFAULT, static_cast<int>(BaseFontSize * GetZoom()));
	m_direct(m_directPtr, SCI_SETWRAPMODE, SC_WRAP_CHAR, 0);
	m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
	m_direct(m_directPtr, SCI_SETMARGINTYPEN, 0, SC_MARGIN_TEXT);
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 1, 0);
	m_direct(m_directPtr, SCI_STYLESETFONT, STYLE_DEFAULT, sptr_t(Utils::ToUtf8(ncm.lfMessageFont.lfFaceName).c_str()));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Debug), RGB(80, 80, 80));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Info), RGB(0, 0, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Warning), RGB(160, 160, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Error), RGB(255, 80, 80));
	m_direct(m_directPtr, SCI_STYLESETBACK, TimeStyle, GetSysColor(COLOR_3DFACE));

	const auto addLogFn = [&](const Misc::Logger::LogItem& item) {
		FILETIME lt;
		SYSTEMTIME st;
		FileTimeToLocalFileTime(&item.timestamp, &lt);
		FileTimeToSystemTime(&lt, &st);
		const auto timestr = Utils::FormatString("%04d-%02d-%02d %02d:%02d:%02d.%03d",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds);
		const auto logstr = item.log + "\n";
		RunOnUiThreadWait([&]() {
			SendMessage(m_hScintilla, WM_SETREDRAW, FALSE, 0);
			m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
			auto nPos = m_direct(m_directPtr, SCI_GETLENGTH, 0, 0);
			auto nLineCount = m_direct(m_directPtr, SCI_GETLINECOUNT, 0, 0) - 1;
			const auto nFirstLine = m_direct(m_directPtr, SCI_GETFIRSTVISIBLELINE, 0, 0);
			const auto nLinesOnScreen = m_direct(m_directPtr, SCI_LINESONSCREEN, 0, 0);
			const auto atBottom = nFirstLine >= nLineCount - nLinesOnScreen && m_direct(m_directPtr, SCI_GETSELECTIONEMPTY, 0, 0);
			m_direct(m_directPtr, SCI_STARTSTYLING, nPos, 0);

			m_direct(m_directPtr, SCI_APPENDTEXT, logstr.length(), sptr_t(logstr.c_str()));
			m_direct(m_directPtr, SCI_SETSTYLING, logstr.length(), static_cast<int>(item.level));
			m_direct(m_directPtr, SCI_MARGINSETTEXT, nLineCount, sptr_t(timestr.c_str()));
			m_direct(m_directPtr, SCI_MARGINSETSTYLE, nLineCount, TimeStyle);
			nPos += logstr.length();
			nLineCount++;
			if (nLineCount > 32768) {
				const auto deleteTo = m_direct(m_directPtr, SCI_POSITIONFROMLINE, nLineCount - 32768, 0);
				m_direct(m_directPtr, SCI_DELETERANGE, 0, deleteTo);
			}
			if (atBottom) {
				m_direct(m_directPtr, SCI_SETFIRSTVISIBLELINE, INT_MAX, 0);
			}
			m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
			SendMessage(m_hScintilla, WM_SETREDRAW, TRUE, 0);
			InvalidateRect(m_hScintilla, nullptr, FALSE);
			return 0;
			});
	};

	const auto& logger = Misc::Logger::GetLogger();
	for (const auto item : logger.GetLogs())
		addLogFn(*item);
	m_callbackHandle = Misc::Logger::GetLogger().OnNewLogItem(addLogFn);

	const auto hMenu = GetSystemMenu(m_hWnd, false);
	InsertMenuW(hMenu, SC_CLOSE, MF_BYCOMMAND | ((GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) ? MF_CHECKED : 0), SystemMenuAlwaysOnTop, L"Always on Top");
	InsertMenuW(hMenu, SC_CLOSE, MF_BYCOMMAND, SystemMenuClear, L"Clear");
	InsertMenuW(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_SEPARATOR, SystemMenuEndSeparator, L"-");

	ShowWindow(m_hWnd, SW_SHOW);
}

App::Window::Log::~Log() {
	Destroy();
}

void App::Window::Log::OnLayout(double zoom, double width, double height) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

LRESULT App::Window::Log::OnNotify(const LPNMHDR nmhdr) {
	if (nmhdr->hwndFrom == m_hScintilla) {
		const auto nm = reinterpret_cast<SCNotification*>(nmhdr);
		if (nmhdr->code == SCN_ZOOM) {
			ResizeMargin();
		}
	}
	return App::Window::Base::OnNotify(nmhdr);
}

LRESULT App::Window::Log::OnSysCommand(WPARAM commandId, short xPos, short yPos) {
	switch (commandId) {
		case SystemMenuAlwaysOnTop:
		{
			HMENU hMenu = GetSystemMenu(m_hWnd, FALSE);
			MENUITEMINFOW menuInfo = { sizeof(MENUITEMINFOW) };
			menuInfo.fMask = MIIM_STATE;
			GetMenuItemInfo(hMenu, SystemMenuAlwaysOnTop, FALSE, &menuInfo);
			if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) {
				SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				menuInfo.fState &= ~MFS_CHECKED;
			} else {
				SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				menuInfo.fState |= MFS_CHECKED;
			}
			SetMenuItemInfoW(hMenu, SystemMenuAlwaysOnTop, FALSE, &menuInfo);
			return 0;
		}
		case SystemMenuClear:
		{
			Misc::Logger::GetLogger().Clear();
			m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
			m_direct(m_directPtr, SCI_CLEARALL, 0, 0);
			m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
			return 0;
		}
	}
	return App::Window::Base::OnSysCommand(commandId, xPos, yPos);
}

void App::Window::Log::OnDestroy() {
	ConfigRepository::Config().ShowLoggingWindow = false;
}

void App::Window::Log::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(130 * GetZoom() * (BaseFontSize + m_direct(m_directPtr, SCI_GETZOOM, 0, 0)) / BaseFontSize));
}
