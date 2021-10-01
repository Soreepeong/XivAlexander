#include "pch.h"
#include "Utils_Win32_TaskDialogBuilder.h"

Utils::Win32::TaskDialog::TaskDialog() = default;

Utils::Win32::TaskDialog::Result Utils::Win32::TaskDialog::Show() {
	m_dialogConfig.lpCallbackData = reinterpret_cast<LONG_PTR>(this);
	m_dialogConfig.pfCallback = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR refData) -> HRESULT {
		return reinterpret_cast<TaskDialog*>(refData)->TaskDialogProc(hwnd, msg, wParam, lParam);
	};

	m_currentRadio = m_dialogConfig.nDefaultButton;
	if (std::ranges::find_if(m_radios, [this](const auto& r) { return r.Id == m_currentRadio; }) == m_radios.end())
		m_currentRadio = m_radios.empty() ? 0 : m_radios.front().Id;
	Result result{};
	BOOL flag{};
	if (const auto hr = TaskDialogIndirect(&m_dialogConfig, &result.Button, &result.Radio, &flag);
		FAILED(hr))
		throw Error(_com_error(hr));
	result.Check = !!flag;
	return result;
}

HRESULT Utils::Win32::TaskDialog::TaskDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	m_currentHwnd = hwnd;

	switch (msg) {
		case TDN_BUTTON_CLICKED: {
			if (const auto it = std::ranges::find_if(m_buttons, [wParam](const auto& r) { return r.Id == static_cast<int>(wParam); }); it != m_buttons.end() && it->Callback)
				return it->Callback(*this);
			break;
		}
		case TDN_CREATED: {
			if (OnCreated)
				OnCreated(*this);
			break;
		}
		case TDN_DESTROYED: {
			if (OnDestroyed)
				OnDestroyed(*this);
			m_currentHwnd = nullptr;
			break;
		}
		case TDN_DIALOG_CONSTRUCTED: {
			if (OnDialogConstructed)
				OnDialogConstructed(*this);
			break;
		}
		case TDN_EXPANDO_BUTTON_CLICKED: {
			if (OnExpandoButtonClicked)
				OnExpandoButtonClicked(*this, wParam);
			break;
		}
		case TDN_HELP: {
			if (OnHelp)
				OnHelp(*this);
			break;
		}
		case TDN_HYPERLINK_CLICKED: {
			const auto link = std::wstring(reinterpret_cast<const wchar_t*>(lParam));
			if (const auto it = m_individualHyperlinkHandlers.find(link);
				it != m_individualHyperlinkHandlers.end()) {
				switch (it->second(*this)) {
					case HyperlinkHandleResult::NotHandled:
						break;

					case HyperlinkHandleResult::HandledKeepDialog:
						return S_OK;

					case HyperlinkHandleResult::HandledCloseDialog:
						PostMessageW(hwnd, WM_CLOSE, 0, 0);
						return S_OK;
				}
			}
			if (m_hyperlinkHandler) {
				switch (m_hyperlinkHandler(*this, link)) {
					case HyperlinkHandleResult::NotHandled:
						break;

					case HyperlinkHandleResult::HandledKeepDialog:
						return S_OK;

					case HyperlinkHandleResult::HandledCloseDialog:
						PostMessageW(hwnd, WM_CLOSE, 0, 0);
						return S_OK;
				}
			}
			if (m_useShellExecuteHyperlinkHandler) {
				SHELLEXECUTEINFOW shex{
					.cbSize = sizeof shex,
					.hwnd = hwnd,
					.lpFile = link.c_str(),
					.nShow = SW_SHOW,
				};
				if (!ShellExecuteExW(&shex)) {
					std::wstring title(64, L'\0');
					while (true) {
						const size_t copied = GetWindowTextW(hwnd, &title[0], static_cast<int>(title.size()));
						if (copied + 1 == title.size())
							title.resize(title.size() * 2);
						else {
							title.resize(copied);
							break;
						}
					}
					MessageBoxF(hwnd, MB_ICONERROR | MB_OK, title.c_str(), std::format(L"Error: {}", Error("ShellExecuteW").what()));
				}
			}
			break;
		}
		case TDN_NAVIGATED: {
			if (OnNavigated)
				OnNavigated(*this);
			break;
		}
		case TDN_RADIO_BUTTON_CLICKED: {
			m_currentRadio = static_cast<int>(wParam);
			if (const auto it = std::ranges::find_if(m_radios, [wParam](const auto& r) { return r.Id == static_cast<int>(wParam); }); it != m_radios.end() && it->Callback)
				return it->Callback(*this);
			break;
		}
		case TDN_TIMER: {
			if (OnTimer)
				OnTimer(*this, static_cast<DWORD>(wParam));
			break;
		}
		case TDN_VERIFICATION_CLICKED: {
			if (OnCheckboxClicked)
				OnCheckboxClicked(*this, wParam);
			break;
		}
	}
	return S_OK;
}

Utils::Win32::TaskDialog Utils::Win32::TaskDialog::Builder::Build() {
	if (m_dialog.m_mainIcon.Icon) {
		m_dialog.m_dialogConfig.hMainIcon = m_dialog.m_mainIcon.Icon;
		m_dialog.m_dialogConfig.dwFlags |= TDF_USE_HICON_MAIN;
	} else {
		m_dialog.m_dialogConfig.pszMainIcon = m_dialog.m_mainIcon.IconRes;
		m_dialog.m_dialogConfig.dwFlags &= ~TDF_USE_HICON_MAIN;
	}

	if (m_dialog.m_footerIcon.Icon) {
		m_dialog.m_dialogConfig.hFooterIcon = m_dialog.m_footerIcon.Icon;
		m_dialog.m_dialogConfig.dwFlags |= TDF_USE_HICON_FOOTER;
	} else {
		m_dialog.m_dialogConfig.pszFooterIcon = m_dialog.m_footerIcon.IconRes;
		m_dialog.m_dialogConfig.dwFlags &= ~TDF_USE_HICON_FOOTER;
	}

	m_dialog.m_dialogConfig.pszWindowTitle = m_dialog.m_windowTitle.StringRes ? m_dialog.m_windowTitle.StringRes : m_dialog.m_windowTitle.Text.c_str();
	m_dialog.m_dialogConfig.pszMainInstruction = m_dialog.m_mainInstruction.StringRes ? m_dialog.m_mainInstruction.StringRes : m_dialog.m_mainInstruction.Text.c_str();
	m_dialog.m_dialogConfig.pszContent = m_dialog.m_content.StringRes ? m_dialog.m_content.StringRes : m_dialog.m_content.Text.c_str();
	m_dialog.m_dialogConfig.pszVerificationText = m_dialog.m_checkbox.StringRes ? m_dialog.m_checkbox.StringRes : m_dialog.m_checkbox.Text.c_str();
	m_dialog.m_dialogConfig.pszExpandedInformation = m_dialog.m_expandedInformation.StringRes ? m_dialog.m_expandedInformation.StringRes : m_dialog.m_expandedInformation.Text.c_str();
	m_dialog.m_dialogConfig.pszExpandedControlText = m_dialog.m_collapsedControlText.StringRes ? m_dialog.m_collapsedControlText.StringRes : m_dialog.m_collapsedControlText.Text.c_str();
	m_dialog.m_dialogConfig.pszCollapsedControlText = m_dialog.m_expandedControlText.StringRes ? m_dialog.m_expandedControlText.StringRes : m_dialog.m_expandedControlText.Text.c_str();
	m_dialog.m_dialogConfig.pszFooter = m_dialog.m_footer.StringRes ? m_dialog.m_footer.StringRes : m_dialog.m_footer.Text.c_str();

	for (const auto& button : m_dialog.m_buttons)
		m_dialog.m_buttonBuffer.emplace_back(button.Id, button.Text.StringRes ? button.Text.StringRes : button.Text.Text.c_str());
	m_dialog.m_dialogConfig.cButtons = static_cast<UINT>(m_dialog.m_buttonBuffer.size());
	m_dialog.m_dialogConfig.pButtons = m_dialog.m_buttonBuffer.data();

	for (const auto& radio : m_dialog.m_radios)
		m_dialog.m_radioBuffer.emplace_back(radio.Id, radio.Text.StringRes ? radio.Text.StringRes : radio.Text.Text.c_str());
	m_dialog.m_dialogConfig.cRadioButtons = static_cast<UINT>(m_dialog.m_radioBuffer.size());
	m_dialog.m_dialogConfig.pRadioButtons = m_dialog.m_radioBuffer.data();

	if (m_dialog.m_hyperlinkHandler || !m_dialog.m_individualHyperlinkHandlers.empty() || m_dialog.m_useShellExecuteHyperlinkHandler)
		m_dialog.m_dialogConfig.dwFlags |= TDF_ENABLE_HYPERLINKS;
	else
		m_dialog.m_dialogConfig.dwFlags &= ~TDF_ENABLE_HYPERLINKS;

	return std::move(m_dialog);
}
