#pragma once

#include <xivres/util.on_dtor.h>
#include "Utils/Win32/Resource.h"

namespace Utils::Win32 {

	class TaskDialog {
	public:
		class Builder;
		friend class Builder;

		enum ActionHandled {
			NotHandled = S_OK,
			Handled = S_FALSE,
		};

		enum class HyperlinkHandleResult {
			NotHandled,
			HandledKeepDialog,
			HandledCloseDialog,
		};

		enum TimerCallbackResult {
			Processed = S_OK,
			ClearCounter = S_FALSE,
		};

		enum class CommandLinkType {
			None = 0,
			Icon = TDF_USE_COMMAND_LINKS,
			NoIcon = TDF_USE_COMMAND_LINKS_NO_ICON,
			Internal_AllBits = TDF_USE_COMMAND_LINKS | TDF_USE_COMMAND_LINKS_NO_ICON
		};

		struct StringOrResId {
			std::wstring Text;
			const wchar_t* StringRes = nullptr;

			StringOrResId() {
			}

			StringOrResId(int stringResId)
				: StringRes(MAKEINTRESOURCEW(stringResId)) {
			}

			StringOrResId(std::wstring_view text)
				: Text(text) {
			}

			StringOrResId(const wchar_t* text)
				: Text(text) {
			}

			StringOrResId(std::wstring text)
				: Text(std::move(text)) {
			}

			StringOrResId(std::string_view text)
				: Text(xivres::util::unicode::convert<std::wstring>(text)) {
			}

			StringOrResId(const std::string& text)
				: Text(xivres::util::unicode::convert<std::wstring>(text)) {
			}

			StringOrResId(const char* text)
				: Text(xivres::util::unicode::convert<std::wstring>(text)) {
			}
		};

		struct HIconOrRes {
			HICON Icon = nullptr;
			const wchar_t* IconRes = nullptr;

			HIconOrRes() {
			}

			HIconOrRes(HICON hIcon)
				: Icon(hIcon) {
			}

			HIconOrRes(int iconResId)
				: IconRes(MAKEINTRESOURCEW(iconResId)) {
			}

			HIconOrRes(const wchar_t* iconRes)
				: IconRes(iconRes) {
			}
		};

		struct Button {
			bool IdSet = false;
			int Id = 0;
			StringOrResId Text;
			std::function<ActionHandled(TaskDialog&)> Callback;
		};

	private:
		TASKDIALOGCONFIG m_dialogConfig{
			.cbSize = sizeof m_dialogConfig,
		};

		std::map<std::wstring, std::function<HyperlinkHandleResult(TaskDialog&)>> m_individualHyperlinkHandlers;
		std::function<HyperlinkHandleResult(TaskDialog&, std::wstring_view)> m_hyperlinkHandler;
		bool m_useShellExecuteHyperlinkHandler = false;

		HIconOrRes m_mainIcon;
		HIconOrRes m_footerIcon;

		StringOrResId m_windowTitle;
		StringOrResId m_mainInstruction;
		StringOrResId m_content;
		StringOrResId m_checkbox;
		StringOrResId m_expandedInformation;
		StringOrResId m_expandedControlText;
		StringOrResId m_collapsedControlText;
		StringOrResId m_footer;

		std::vector<Button> m_buttons;
		std::vector<Button> m_radios;
		std::vector<TASKDIALOG_BUTTON> m_buttonBuffer;
		std::vector<TASKDIALOG_BUTTON> m_radioBuffer;

		int m_currentRadio{};
		HWND m_currentHwnd{};

		TaskDialog();

	public:
		HWND GetHwnd() const { return m_currentHwnd; }

		xivres::util::on_dtor WithHiddenDialog() {
			ShowWindow(m_currentHwnd, SW_HIDE);
			return {
				[this]() {
					ShowWindow(m_currentHwnd, SW_SHOW);
				}
			};
		}

		std::function<void(TaskDialog&)> OnCreated;
		std::function<void(TaskDialog&)> OnDestroyed;
		std::function<void(TaskDialog&)> OnDialogConstructed;
		std::function<void(TaskDialog&, bool)> OnExpandoButtonClicked;
		std::function<void(TaskDialog&)> OnHelp;
		std::function<void(TaskDialog&)> OnNavigated;
		std::function<TimerCallbackResult(TaskDialog&, DWORD)> OnTimer;
		std::function<void(TaskDialog&, bool)> OnCheckboxClicked;

		struct Result {
			int Button;
			int Radio;
			bool Check;
		};

		Result Show();

	private:
		HRESULT TaskDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};

	class TaskDialog::Builder {
		TaskDialog m_dialog;

		int m_idCounter = 100;

	public:
		Builder& WithInstance(HINSTANCE hInstance) {
			m_dialog.m_dialogConfig.hInstance = hInstance;
			return *this;
		}

		Builder& WithParentWindow(HWND hWndParent) {
			m_dialog.m_dialogConfig.hwndParent = hWndParent;
			return *this;
		}

		Builder& WithAllowDialogCancellation(bool set = true) {
			return WithToggleFlag(TDF_ALLOW_DIALOG_CANCELLATION, set);
		}

		Builder& WithCallbackTimer(bool set = true) {
			return WithToggleFlag(TDF_CALLBACK_TIMER, set);
		}

		Builder& WithCommonButton(_TASKDIALOG_COMMON_BUTTON_FLAGS id, bool set = true) {
			m_dialog.m_dialogConfig.dwCommonButtons = (m_dialog.m_dialogConfig.dwCommonButtons & ~id) | (set ? id : 0U);
			return *this;
		}

		Builder& WithWindowTitle(StringOrResId s) {
			m_dialog.m_windowTitle = std::move(s);
			return *this;
		}

		Builder& WithMainIcon(HIconOrRes s) {
			m_dialog.m_mainIcon = s;
			return *this;
		}

		Builder& WithMainInstruction(StringOrResId s) {
			m_dialog.m_mainInstruction = std::move(s);
			return *this;
		}

		Builder& WithContent(StringOrResId s) {
			m_dialog.m_content = std::move(s);
			return *this;
		}

		Builder& WithButton(Button b) {
			if (!b.IdSet) {
				do {
					b.Id = m_idCounter++;
				} while (IdExists(b.Id));
				b.IdSet = true;
			}
			m_dialog.m_buttons.emplace_back(std::move(b));
			return *this;
		}

		Builder& WithButtonDefault(int id) {
			m_dialog.m_dialogConfig.nDefaultButton = id;
			return *this;
		}

		Builder& WithButtonCommandLinks(CommandLinkType type = CommandLinkType::Icon) {
			return WithToggleFlags(CommandLinkType::Internal_AllBits, type);
		}

		Builder& WithFooter(StringOrResId s) {
			m_dialog.m_footer = std::move(s);
			return *this;
		}

		TaskDialog Build();

	private:
		template<typename T>
		Builder& WithToggleFlag(T bits, bool use) {
			m_dialog.m_dialogConfig.dwFlags = (m_dialog.m_dialogConfig.dwFlags & ~static_cast<DWORD>(bits)) | (use ? static_cast<DWORD>(bits) : 0U);
			return *this;
		}

		template<typename T>
		Builder& WithToggleFlags(T clearBits, T setBits) {
			m_dialog.m_dialogConfig.dwFlags = (m_dialog.m_dialogConfig.dwFlags & ~static_cast<DWORD>(clearBits)) | static_cast<DWORD>(setBits);
			return *this;
		}

		[[nodiscard]] bool IdExists(int id) const {
			return std::ranges::any_of(m_dialog.m_buttons, [id](const auto& r) { return r.Id == id; })
				|| std::ranges::any_of(m_dialog.m_radios, [id](const auto& r) { return r.Id == id; });
		}
	};
}
