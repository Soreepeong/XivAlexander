#include "pch.h"
#include "App_Window_Log.h"
#include "resource.h"

constexpr int BaseFontSize = 9;

static WNDCLASSEXW WindowClass() {
	Utils::Win32Handle<HICON, DestroyIcon> hIcon(LoadIcon(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)));
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_LOG_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::LogM";
	wcex.hIconSm = hIcon;
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
	m_direct(m_directPtr, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 1, 0);
	m_direct(m_directPtr, SCI_STYLESETFONT, STYLE_DEFAULT, sptr_t(Utils::ToUtf8(ncm.lfMessageFont.lfFaceName).c_str()));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Debug), RGB(80, 80, 80));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Info), RGB(0, 0, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Warning), RGB(160, 160, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, static_cast<int>(Misc::Logger::LogLevel::Error), RGB(255, 80, 80));

	const auto addLogFn = [&](const Misc::Logger::LogItem& item) {
		FILETIME lt;
		SYSTEMTIME st;
		FileTimeToLocalFileTime(&item.timestamp, &lt);
		FileTimeToSystemTime(&lt, &st);
		const auto logstr = Utils::FormatString("%04d-%02d-%02d %02d:%02d:%02d.%03d\t%s\n",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds,
			item.log.c_str());
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

	ShowWindow(m_hWnd, SW_SHOW);
}

App::Window::Log::~Log() {
	Destroy();
}

void App::Window::Log::OnLayout(double zoom, double width, double height) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

LRESULT App::Window::Log::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_COMMAND:
		{
			switch (LOWORD(wParam)) {
				case ID_FILE_SAVE:
				{
					struct DataT {
						HWND hWnd;
						std::string buf;
					};

					DataT *pDataT = new DataT();
					pDataT->hWnd = m_hWnd;
					pDataT->buf = std::string(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
					m_direct(m_directPtr, SCI_GETTEXT, pDataT->buf.length(), reinterpret_cast<sptr_t>(&pDataT->buf[0]));
					pDataT->buf.resize(pDataT->buf.length() - 1);

					Utils::Win32Handle<> hThread(CreateThread(nullptr, 0, [](void* pDataRaw) -> DWORD {
						DataT* pDataT = reinterpret_cast<DataT*>(pDataRaw);
						Utils::CallOnDestruction freeDataT([pDataT]() { delete pDataT; });
						const COMDLG_FILTERSPEC saveFileTypes[] = {
							{L"Log Files (*.log)",		L"*.log"},
							{L"All Documents (*.*)",	L"*.*"}
						};

						auto throw_on_error = [](HRESULT val) { 
							if (!SUCCEEDED(val))
								_com_raise_error(val); 
						};

						try {
							_com_ptr_t<_com_IIID<IFileSaveDialog, &__uuidof(IFileSaveDialog)>> pDialog;
							DWORD dwFlags;
							throw_on_error(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
							throw_on_error(pDialog->SetFileTypes(ARRAYSIZE(saveFileTypes), saveFileTypes));
							throw_on_error(pDialog->SetFileTypeIndex(0));
							throw_on_error(pDialog->SetDefaultExtension(L"log"));
							throw_on_error(pDialog->GetOptions(&dwFlags));
							throw_on_error(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
							if (SUCCEEDED(pDialog->Show(pDataT->hWnd))) {
								_com_ptr_t<_com_IIID<IShellItem, &__uuidof(IShellItem)>> pResult;
								PWSTR pszNewFileName;
								throw_on_error(pDialog->GetResult(&pResult));
								throw_on_error(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszNewFileName));
								if (!pszNewFileName)
									throw std::exception("The selected file does not have a filesystem path.");

								Utils::CallOnDestruction freeFileName([pszNewFileName]() { CoTaskMemFree(pszNewFileName); });

								Utils::Win32Handle<> hFile(CreateFile(pszNewFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
								DWORD written;
								WriteFile(hFile, pDataT->buf.data(), static_cast<DWORD>(pDataT->buf.length()), &written, nullptr);
								if (written != pDataT->buf.length())
									throw std::exception("Failed to fully write the log file.");

								MessageBoxW(pDataT->hWnd, Utils::FormatString(L"Log saved to %s.", pszNewFileName).c_str(), L"XivAlexander", MB_ICONINFORMATION);
							}
						} catch (std::exception& e) {
							MessageBoxW(pDataT->hWnd, Utils::FromUtf8(e.what()).c_str(), L"XivAlexander", MB_ICONERROR);
						} catch (_com_error& e) {
							MessageBoxW(pDataT->hWnd, e.Description(), L"XivAlexander", MB_ICONERROR);
						}
						return 0;
						}, pDataT, 0, nullptr));
					return 0;
				}
				
				case ID_FILE_CLEAR:
				{
					Misc::Logger::GetLogger().Clear();
					m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
					m_direct(m_directPtr, SCI_CLEARALL, 0, 0);
					m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
					return 0;
				}

				case ID_VIEW_ALWAYSONTOP:
				{
					HMENU hMenu = GetMenu(m_hWnd);
					MENUITEMINFOW menuInfo = { sizeof(MENUITEMINFOW) };
					menuInfo.fMask = MIIM_STATE;
					GetMenuItemInfo(hMenu, ID_VIEW_ALWAYSONTOP, FALSE, &menuInfo);
					if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) {
						SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
						menuInfo.fState &= ~MFS_CHECKED;
					} else {
						SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
						menuInfo.fState |= MFS_CHECKED;
					}
					SetMenuItemInfoW(hMenu, ID_VIEW_ALWAYSONTOP, FALSE, &menuInfo);
				}
			}
			break;
		}
	}
	return Base::WndProc(uMsg, wParam, lParam);
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

void App::Window::Log::OnDestroy() {
	ConfigRepository::Config().ShowLoggingWindow = false;
}

void App::Window::Log::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(m_direct(m_directPtr, SCI_TEXTWIDTH, uptr_t(STYLE_LINENUMBER), reinterpret_cast<sptr_t>("999999"))));
}
