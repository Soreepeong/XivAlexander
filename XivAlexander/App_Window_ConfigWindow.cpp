#include "pch.h"
#include "App_Window_ConfigWindow.h"
#include "resource.h"

constexpr int BaseFontSize = 11;

static WNDCLASSEXW WindowClass() {
	const auto hIcon = Utils::Win32::Icon(
		LoadIconW(Dll::Module(), MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"Failed to load app icon.");
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = Dll::Module();
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_CONFIG_EDITOR_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::Config";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::Config::Config(App::Config::BaseRepository* pRepository)
	: BaseWindow(WindowClass(), L"Config", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr)
	, m_pRepository(pRepository) {
	m_hScintilla = CreateWindowExW(0, TEXT("Scintilla"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
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
	return BaseWindow::OnNotify(nmhdr);
}

void App::Window::Config::Revert() {
	m_pRepository->Save();
	nlohmann::json config;
	try {
		std::ifstream in(m_pRepository->GetConfigPath());
		in >> config;
		m_originalConfig = config.dump(1, '\t');
		m_direct(m_directPtr, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(m_originalConfig.data()));
	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(m_hWnd, MB_ICONERROR, L"XivAlexander", L"Failed to reload previous configuration file: {}", e.what());
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
		Utils::Win32::MessageBoxF(m_hWnd, MB_ICONERROR, L"XivAlexander", L"Invalid JSON: {}", e.what());
		return false;
	}
	try {
		std::ofstream out(m_pRepository->GetConfigPath());
		out << buf;
	} catch (std::exception& e) {
		Utils::Win32::MessageBoxF(m_hWnd, MB_OK, L"XivAlexander", L"Failed to save new configuration file: {}", e.what());
		return false;
	}
	m_originalConfig = std::move(buf);
	m_direct(m_directPtr, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(&m_originalConfig[0]));
	m_pRepository->Reload(true);
	return true;
}

App::Config::BaseRepository* App::Window::Config::GetRepository() const {
	return m_pRepository;
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
				switch (Utils::Win32::MessageBoxF(m_hWnd, MB_YESNOCANCEL | MB_ICONQUESTION, L"XivAlexander", L"Apply changed configuration?")) {
					case IDCANCEL:
						return 0;
					case IDYES:
						if (!TrySave())
							return 0;
				}
			}
			break;
	}
	return BaseWindow::WndProc(uMsg, wParam, lParam);
}

void App::Window::Config::OnLayout(double zoom, double width, double height) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

void App::Window::Config::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(m_direct(m_directPtr, SCI_TEXTWIDTH, uptr_t(STYLE_LINENUMBER), reinterpret_cast<sptr_t>("999999"))));
}
