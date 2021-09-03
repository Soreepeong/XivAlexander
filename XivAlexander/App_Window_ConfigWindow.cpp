#include "pch.h"
#include "App_Window_ConfigWindow.h"

#include <XivAlexanderCommon/Utils_Win32_Closeable.h>

#include "App_Misc_Logger.h"
#include "DllMain.h"
#include "resource.h"
#include "XivAlexanderCommon/Utils_Win32_Resource.h"

constexpr int BaseFontSize = 11;

static WNDCLASSEXW WindowClass() {
	const auto hIcon = Utils::Win32::Icon(
		LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"LoadIconW");
	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = Dll::Module();
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_CONFIG_EDITOR_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::ConfigWindow";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::ConfigWindow::ConfigWindow(UINT nTitleStringResourceId, Config::BaseRepository* pRepository)
	: BaseWindow(WindowClass(), nullptr, WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr)
	, m_pRepository(pRepository)
	, m_nTitleStringResourceId(nTitleStringResourceId) {

	m_hScintilla = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 0, 0, 0, m_hWnd, nullptr, Dll::Module(), nullptr);
	m_direct = reinterpret_cast<SciFnDirect>(SendMessageW(m_hScintilla, SCI_GETDIRECTFUNCTION, 0, 0));
	m_directPtr = SendMessageW(m_hScintilla, SCI_GETDIRECTPOINTER, 0, 0);
	m_direct(m_directPtr, SCI_STYLESETSIZE, STYLE_DEFAULT, static_cast<int>(BaseFontSize * GetZoom()));
	m_direct(m_directPtr, SCI_SETWRAPMODE, SC_WRAP_CHAR, 0);
	m_direct(m_directPtr, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 1, 0);
	m_direct(m_directPtr, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>("Consolas"));
	m_direct(m_directPtr, SCI_SETLEXERLANGUAGE, 0, reinterpret_cast<sptr_t>("json"));

	Revert();
	ApplyLanguage(m_config->Runtime.GetLangId());
	ShowWindow(m_hWnd, SW_SHOW);
	SetFocus(m_hScintilla);
}

App::Window::ConfigWindow::~ConfigWindow() {
	Destroy();
}

LRESULT App::Window::ConfigWindow::OnNotify(const LPNMHDR nmhdr) {
	if (nmhdr->hwndFrom == m_hScintilla) {
		const auto nm = reinterpret_cast<SCNotification*>(nmhdr);
		if (nmhdr->code == SCN_ZOOM) {
			ResizeMargin();
		}
	}
	return BaseWindow::OnNotify(nmhdr);
}

void App::Window::ConfigWindow::Revert() {
	m_pRepository->Save();
	std::string buffer;
	try {
		try {
			std::ifstream in(m_pRepository->GetConfigPath());
			in.seekg(0, std::ios::end);
			const auto len = static_cast<size_t>(in.tellg());
			buffer.resize(len, 0);
			in.seekg(0);
			in.read(&buffer[0], buffer.size());
		} catch (...) {
			buffer = "{}";
		}
		buffer = nlohmann::json::parse(buffer).dump(1, '\t');
	} catch (std::exception& e) {
		m_logger->Format(LogCategory::General, m_config->Runtime.GetLangId(), IDS_ERROR_CONFIGURATION_LOAD, e.what());
	}
	m_originalConfig = std::move(buffer);
	m_direct(m_directPtr, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(&m_originalConfig[0]));
}

bool App::Window::ConfigWindow::TrySave() {
	std::string buf(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
	m_direct(m_directPtr, SCI_GETTEXT, buf.length(), reinterpret_cast<sptr_t>(&buf[0]));
	buf.resize(buf.length() - 1);
	try {
		buf = nlohmann::json::parse(buf).dump(1, '\t');
	} catch (nlohmann::json::exception& e) {
		Utils::Win32::MessageBoxF(m_hWnd, MB_ICONERROR, m_config->Runtime.GetStringRes(IDS_APP_NAME),
			m_config->Runtime.FormatStringRes(IDS_ERROR_CONFIGURATION_SAVE, e.what()));
		return false;
	}
	try {
		std::ofstream out(m_pRepository->GetConfigPath());
		out << buf;
	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(m_hWnd, MB_OK, m_config->Runtime.GetStringRes(IDS_APP_NAME),
			m_config->Runtime.FormatStringRes(IDS_ERROR_CONFIGURATION_SAVE, e.what()));
		return false;
	}
	m_originalConfig = std::move(buf);
	m_direct(m_directPtr, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(&m_originalConfig[0]));
	m_pRepository->Reload(true);
	return true;
}

App::Config::BaseRepository* App::Window::ConfigWindow::GetRepository() const {
	return m_pRepository;
}

void App::Window::ConfigWindow::ApplyLanguage(WORD languageId) {
	m_hAcceleratorWindow = { Dll::Module(), RT_ACCELERATOR, MAKEINTRESOURCE(IDR_CONFIG_EDITOR_ACCELERATOR), languageId };
	SetWindowTextW(m_hWnd, m_config->Runtime.GetStringRes(m_nTitleStringResourceId));
	Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCE(IDR_CONFIG_EDITOR_MENU), languageId).AttachAndSwap(m_hWnd);
}

LRESULT App::Window::ConfigWindow::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_INITMENUPOPUP:
		{
			Utils::Win32::SetMenuState(GetMenu(m_hWnd), ID_VIEW_ALWAYSONTOP, GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST, true);
			break;
		}

		case WM_ACTIVATE:
			SetFocus(m_hScintilla);
			break;

		case WM_COMMAND:
		{
			switch (LOWORD(wParam)) {
				case ID_FILE_SAVE:
					TrySave();
					return 0;

				case ID_FILE_REVERT:
					Revert();
					return 0;

				case ID_FILE_CLOSE:
					SendMessageW(m_hWnd, WM_CLOSE, 0, 0);
					return 0;

				case ID_VIEW_ALWAYSONTOP:
					if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
						SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					else
						SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
					return 0;
			}
			break;
		}

		case WM_CLOSE:
			std::string buf(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
			m_direct(m_directPtr, SCI_GETTEXT, buf.length(), reinterpret_cast<sptr_t>(&buf[0]));
			buf.resize(buf.length() - 1);
			if (buf != m_originalConfig) {
				switch (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNOCANCEL | MB_ICONQUESTION, m_config->Runtime.GetStringRes(IDS_APP_NAME),
					m_config->Runtime.GetStringRes(IDS_CONFIRM_CONFIG_WINDOW_CLOSE))) {
					case IDCANCEL:
						return 0;
					case IDYES:
						if (!TrySave())
							return 0;
				}
			}
			break;
	}
	return BaseWindow::WndProc(hwnd, uMsg, wParam, lParam);
}

void App::Window::ConfigWindow::OnLayout(double zoom, double width, double height) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

void App::Window::ConfigWindow::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(m_direct(m_directPtr, SCI_TEXTWIDTH, uptr_t(STYLE_LINENUMBER), reinterpret_cast<sptr_t>("999999"))));
}
