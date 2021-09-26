#include "pch.h"
#include "App_Window_LogWindow.h"

#include <XivAlexanderCommon/Utils_Win32_Closeable.h>
#include <XivAlexanderCommon/Utils_Win32_Resource.h>

#include "App_ConfigRepository.h"
#include "App_Misc_Logger.h"
#include "DllMain.h"
#include "resource.h"

constexpr int BaseFontSize = 9;
constexpr auto MaxDisplayedLines = 256 * 1024;

static const std::map<App::LogLevel, int> LogLevelStyleMap{
	{App::LogLevel::Debug, STYLE_LASTPREDEFINED + 0},
	{App::LogLevel::Info, STYLE_LASTPREDEFINED + 1},
	{App::LogLevel::Warning, STYLE_LASTPREDEFINED + 2},
	{App::LogLevel::Error, STYLE_LASTPREDEFINED + 3},
};

static const std::map<App::LogCategory, const char*> LogCategoryNames{
	{App::LogCategory::General, "General"},
	{App::LogCategory::SocketHook, "SocketHook"},
	{App::LogCategory::AllIpcMessageLogger, "AllIpcMessageLogger"},
	{App::LogCategory::AnimationLockLatencyHandler, "AnimationLockLatencyHandler"},
	{App::LogCategory::EffectApplicationDelayLogger, "EffectApplicationDelayLogger"},
	{App::LogCategory::IpcTypeFinder, "IpcTypeFinder"},
	{App::LogCategory::GameResourceOverrider, "GameResourceOverrider"},
};

static WNDCLASSEXW WindowClass() {
	const auto hIcon = Utils::Win32::Icon(LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"LoadIconW");
	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = Dll::Module();
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszClassName = L"XivAlexander::Window::LogWindow";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::LogWindow::LogWindow()
	: BaseWindow(WindowClass(), nullptr, WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr) {

	NONCLIENTMETRICS ncm = {sizeof(NONCLIENTMETRICS)};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

	m_hScintilla = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 0, 0, 0, m_hWnd, nullptr, Dll::Module(), nullptr);
	m_direct = reinterpret_cast<SciFnDirect>(SendMessageW(m_hScintilla, SCI_GETDIRECTFUNCTION, 0, 0));
	m_directPtr = SendMessageW(m_hScintilla, SCI_GETDIRECTPOINTER, 0, 0);
	m_direct(m_directPtr, SCI_STYLESETSIZE, STYLE_DEFAULT, static_cast<int>(BaseFontSize * GetZoom()));
	m_direct(m_directPtr, SCI_SETWRAPMODE, SC_WRAP_CHAR, 0);
	m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
	m_direct(m_directPtr, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 1, 0);
	m_direct(m_directPtr, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(Utils::ToUtf8(ncm.lfMessageFont.lfFaceName).data()));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Debug), RGB(80, 80, 80));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Info), RGB(0, 0, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Warning), RGB(160, 160, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Error), RGB(255, 80, 80));

	const auto addLogFn = [&](const std::deque<Misc::Logger::LogItem>& items) {
		std::stringstream o;
		auto level = LogLevel::Unset;
		for (const auto& item : items) {
			if (item.id <= m_lastDisplayedLogId)
				continue;
			m_lastDisplayedLogId = item.id;
			if (level != item.level) {
				if (o.tellp())
					FlushLog(o.str(), level);
				o.clear();
				level = item.level;
			}
			const auto st = item.TimestampAsLocalSystemTime();
			const auto logstr = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}\t{}\t{}\n",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond,
				st.wMilliseconds,
				LogCategoryNames.at(item.category),
				item.log);
			o << logstr;
		}
		if (o.tellp())
			FlushLog(o.str(), level);
	};

	m_cleanup += m_config->Runtime.AlwaysOnTop_XivAlexLogWindow.OnChangeListener([this](auto&) {
		SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexLogWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	});

	m_logger->WithLogs([&](const auto& logs) { addLogFn(logs); });
	m_cleanup += m_logger->OnNewLogItem(addLogFn);

	ApplyLanguage(m_config->Runtime.GetLangId());
	ShowWindow(m_hWnd, SW_SHOW);
	SetWindowPos(m_hWnd, m_config->Runtime.AlwaysOnTop_XivAlexLogWindow ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SetFocus(m_hScintilla);
}

App::Window::LogWindow::~LogWindow() {
	Destroy();
}

void App::Window::LogWindow::ApplyLanguage(WORD languageId) {
	m_hAcceleratorWindow = {Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_LOG_ACCELERATOR), languageId};
	SetWindowTextW(m_hWnd, m_config->Runtime.GetStringRes(IDS_WINDOW_LOG));
	Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_LOG_MENU), languageId).AttachAndSwap(m_hWnd);
}

void App::Window::LogWindow::OnLayout(double zoom, double width, double height, int resizeType) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

LRESULT App::Window::LogWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_INITMENUPOPUP: {
			Utils::Win32::SetMenuState(GetMenu(m_hWnd), ID_VIEW_ALWAYSONTOP, m_config->Runtime.AlwaysOnTop_XivAlexLogWindow, true);
			break;
		}
		case WM_ACTIVATE:
			SetFocus(m_hScintilla);
			break;
		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case ID_FILE_SAVE: {
					const auto hWnd = m_hWnd;
					std::string buf(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
					m_direct(m_directPtr, SCI_GETTEXT, buf.length(), reinterpret_cast<sptr_t>(&buf[0]));
					buf.resize(buf.length() - 1);

					static const COMDLG_FILTERSPEC saveFileTypes[] = {
						{L"Log Files (*.log)", L"*.log"},
						{L"All Documents (*.*)", L"*.*"}
					};

					try {
						IFileSaveDialogPtr pDialog;
						DWORD dwFlags;
						Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
						Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(ARRAYSIZE(saveFileTypes), saveFileTypes));
						Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypeIndex(0));
						Utils::Win32::Error::ThrowIfFailed(pDialog->SetDefaultExtension(L"log"));
						Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
						Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
						Utils::Win32::Error::ThrowIfFailed(pDialog->Show(hWnd), true);

						std::filesystem::path newFileName;
						{
							IShellItemPtr pResult;
							PWSTR pszNewFileName;
							Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
							Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszNewFileName));
							if (!pszNewFileName)
								throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");
							newFileName = pszNewFileName;
						}

						Utils::SaveToFile(newFileName, buf);
						Utils::Win32::MessageBoxF(hWnd, MB_ICONINFORMATION, m_config->Runtime.GetStringRes(IDS_APP_NAME),
							m_config->Runtime.FormatStringRes(IDS_LOG_SAVED, newFileName));

					} catch (const Utils::Win32::CancelledError&) {
						return 0;

					} catch (const std::exception& e) {
						Utils::Win32::MessageBoxF(hWnd, MB_ICONERROR, m_config->Runtime.GetStringRes(IDS_APP_NAME),
							m_config->Runtime.FormatStringRes(IDS_ERROR_LOG_SAVE, e.what()));
					}
					return 0;
				}

				case ID_FILE_CLEAR: {
					m_logger->Clear();
					m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
					m_direct(m_directPtr, SCI_CLEARALL, 0, 0);
					m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
					return 0;
				}

				case ID_FILE_CLOSE: {
					SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
					return 0;
				}

				case ID_VIEW_ALWAYSONTOP: {
					m_config->Runtime.AlwaysOnTop_XivAlexLogWindow = !m_config->Runtime.AlwaysOnTop_XivAlexLogWindow;
					return 0;
				}
			}
			break;
		}
	}
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
}

LRESULT App::Window::LogWindow::OnNotify(const LPNMHDR nmhdr) {
	if (nmhdr->hwndFrom == m_hScintilla) {
		const auto nm = reinterpret_cast<SCNotification*>(nmhdr);
		if (nmhdr->code == SCN_ZOOM) {
			ResizeMargin();
		}
	}
	return BaseWindow::OnNotify(nmhdr);
}

void App::Window::LogWindow::OnDestroy() {
	m_config->Runtime.ShowLoggingWindow = false;
}

void App::Window::LogWindow::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(m_direct(m_directPtr, SCI_TEXTWIDTH, static_cast<uptr_t>(STYLE_LINENUMBER), reinterpret_cast<sptr_t>("999999"))));
}

void App::Window::LogWindow::FlushLog(const std::string& logstr, LogLevel level) {
	RunOnUiThreadWait([&]() {
		SendMessageW(m_hScintilla, WM_SETREDRAW, FALSE, 0);
		m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
		const auto nPos = m_direct(m_directPtr, SCI_GETLENGTH, 0, 0);
		auto nLineCount = m_direct(m_directPtr, SCI_GETLINECOUNT, 0, 0) - 1;
		const auto nFirstLine = m_direct(m_directPtr, SCI_GETFIRSTVISIBLELINE, 0, 0);
		const auto nLinesOnScreen = m_direct(m_directPtr, SCI_LINESONSCREEN, 0, 0);
		const auto atBottom = nFirstLine >= nLineCount - nLinesOnScreen && m_direct(m_directPtr, SCI_GETSELECTIONEMPTY, 0, 0);
		m_direct(m_directPtr, SCI_STARTSTYLING, nPos, 0);

		m_direct(m_directPtr, SCI_APPENDTEXT, logstr.length(), reinterpret_cast<sptr_t>(logstr.data()));
		m_direct(m_directPtr, SCI_SETSTYLING, logstr.length(), LogLevelStyleMap.at(level));
		nLineCount++;
		if (nLineCount > MaxDisplayedLines) {
			const auto deleteTo = m_direct(m_directPtr, SCI_POSITIONFROMLINE, nLineCount - 32768, 0);
			m_direct(m_directPtr, SCI_DELETERANGE, 0, deleteTo);
		}
		if (atBottom) {
			m_direct(m_directPtr, SCI_SETFIRSTVISIBLELINE, INT_MAX, 0);
		}
		m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
		SendMessageW(m_hScintilla, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(m_hScintilla, nullptr, FALSE);
		return 0;
	});
}
