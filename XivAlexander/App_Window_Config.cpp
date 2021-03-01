#include "pch.h"
#include "App_Window_Config.h"
#include "resource.h"

std::unique_ptr<App::Window::Config> App::Window::Config::m_pConfigWindow;
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
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_CONFIG_EDITOR_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::Config";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::Config::Config()
	: Base(WindowClass(), L"Config", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr) {

	NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

	m_hScintilla = CreateWindowExW(0, TEXT("Scintilla"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 0, 0, 0, m_hWnd, nullptr, g_hInstance, nullptr);
	m_direct = reinterpret_cast<SciFnDirect>(SendMessageW(m_hScintilla, SCI_GETDIRECTFUNCTION, 0, 0));
	m_directPtr = static_cast<sptr_t>(SendMessageW(m_hScintilla, SCI_GETDIRECTPOINTER, 0, 0));
	m_direct(m_directPtr, SCI_STYLESETSIZE, STYLE_DEFAULT, static_cast<int>(BaseFontSize * GetZoom()));
	m_direct(m_directPtr, SCI_SETWRAPMODE, SC_WRAP_CHAR, 0);
	m_direct(m_directPtr, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 1, 0);
	m_direct(m_directPtr, SCI_STYLESETFONT, STYLE_DEFAULT, sptr_t(Utils::ToUtf8(ncm.lfMessageFont.lfFaceName).c_str()));
	m_direct(m_directPtr, SCI_SETLEXERLANGUAGE, 0, sptr_t("json"));

	Revert();
	ShowWindow(m_hWnd, SW_SHOW);
}

App::Window::Config::~Config() {
	Destroy();
}

LRESULT App::Window::Config::OnNotify(const LPNMHDR nmhdr) {
	if (nmhdr->hwndFrom == m_hScintilla) {
		const auto nm = reinterpret_cast<SCNotification*>(nmhdr);
		if (nmhdr->code == SCN_ZOOM) {
			ResizeMargin();
		}
	}
	return App::Window::Base::OnNotify(nmhdr);
}

void App::Window::Config::Revert() {
	ConfigRepository::Config().Save();
	nlohmann::json config;
	try {
		std::ifstream in(ConfigRepository::Config().GetConfigPath());
		in >> config;
		m_originalConfig = config.dump(1, '\t');
		m_direct(m_directPtr, SCI_SETTEXT, 0, sptr_t(m_originalConfig.c_str()));
	} catch (std::exception& e) {
		MessageBox(m_hWnd, Utils::FormatString(L"JSON Config load error: %s", Utils::FromUtf8(e.what())).c_str(), L"XivAlexander", MB_ICONERROR);
		DestroyWindow(m_hWnd);
	}
}

bool App::Window::Config::TrySave() {
	std::string buf(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
	m_direct(m_directPtr, SCI_GETTEXT, buf.length(), reinterpret_cast<sptr_t>(&buf[0]));
	buf.resize(buf.length() - 1);
	try {
		buf = nlohmann::json::parse(buf).dump(1, '\t');
	} catch (nlohmann::json::exception& e) {
		MessageBox(m_hWnd, Utils::FormatString(L"Invalid JSON: %s", Utils::FromUtf8(e.what()).c_str()).c_str(), L"XivAlexander", MB_ICONERROR);
		return false;
	}
	try {
		std::ofstream out(ConfigRepository::Config().GetConfigPath());
		out << buf;
	} catch (std::exception& e) {
		MessageBox(m_hWnd, Utils::FormatString(L"JSON Config save error: %s", Utils::FromUtf8(e.what()).c_str()).c_str(), L"XivAlexander", MB_ICONERROR);
		return false;
	}
	ConfigRepository::Config().Reload(true);
	return true;
}

LRESULT App::Window::Config::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_COMMAND:
		{
			switch (LOWORD(wParam)) {
				case ID_FILE_APPLY:
					TrySave();
					return 0;
				case ID_FILE_REVERT:
					Revert();
					return 0;
			}
			break;
		}
		case WM_CLOSE:
			std::string buf(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
			m_direct(m_directPtr, SCI_GETTEXT, buf.length(), reinterpret_cast<sptr_t>(&buf[0]));
			buf.resize(buf.length() - 1);
			if (buf != m_originalConfig) {
				auto res = MessageBox(m_hWnd, L"Apply changed configuration?", L"XivAlexander", MB_YESNOCANCEL | MB_ICONQUESTION);
				if (res == IDCANCEL) {
					return 0;
				} else if (res == IDYES) {
					if (!TrySave()) {
						return 0;
					}
				}
			}
			break;
	}
	return Base::WndProc(uMsg, wParam, lParam);
}

void App::Window::Config::OnLayout(double zoom, double width, double height) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

void App::Window::Config::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(m_direct(m_directPtr, SCI_TEXTWIDTH, uptr_t(STYLE_LINENUMBER), reinterpret_cast<sptr_t>("999999"))));
}
