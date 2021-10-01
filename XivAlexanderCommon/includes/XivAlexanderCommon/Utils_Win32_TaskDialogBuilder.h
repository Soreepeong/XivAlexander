#pragma once

#include "Utils_CallOnDestruction.h"
#include "Utils_Win32_Resource.h"

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

		enum class ProgressType {
			None = 0,
			Determinate = TDF_SHOW_PROGRESS_BAR,
			Indeterminate = TDF_SHOW_MARQUEE_PROGRESS_BAR,
			Internal_AllBits = TDF_SHOW_PROGRESS_BAR | TDF_SHOW_MARQUEE_PROGRESS_BAR
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
				: Text(FromUtf8(text)) {
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

		CallOnDestruction WithHiddenDialog() {
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
		} Show();

	private:
		HRESULT TaskDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};

	class TaskDialog::Builder {
		WORD m_languageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
		TaskDialog m_dialog;

		int m_idCounter = 100;

	public:
		Builder& WithInstance(HINSTANCE hInstance) {
			m_dialog.m_dialogConfig.hInstance = hInstance;
			return *this;
		}

		Builder& WithTextLangId(WORD wLanguageId) {
			m_languageId = wLanguageId;
			return *this;
		}

		Builder& WithParentWindow(HWND hWndParent) {
			m_dialog.m_dialogConfig.hwndParent = hWndParent;
			return *this;
		}

		Builder& WithHyperlinkHandler(const std::wstring& link, std::function<HyperlinkHandleResult(TaskDialog&)> callback) {
			if (callback)
				m_dialog.m_individualHyperlinkHandlers.insert_or_assign(link, std::move(callback));
			else
				m_dialog.m_individualHyperlinkHandlers.erase(link);
			return *this;
		}

		Builder& WithHyperlinkHandler(std::function<HyperlinkHandleResult(TaskDialog&, std::wstring_view)> callback) {
			m_dialog.m_hyperlinkHandler = std::move(callback);
			return *this;
		}

		Builder& WithHyperlinkShellExecute(bool set = true) {
			m_dialog.m_useShellExecuteHyperlinkHandler = set;
			return *this;
		}

		Builder& WithAllowDialogCancellation(bool set = true) {
			return WithToggleFlag(TDF_ALLOW_DIALOG_CANCELLATION, set);
		}

		Builder& WithProgressBar(ProgressType type = ProgressType::Determinate) {
			return WithToggleFlags(ProgressType::Internal_AllBits, type);
		}

		Builder& WithCallbackTimer(bool set = true) {
			return WithToggleFlag(TDF_CALLBACK_TIMER, set);
		}

		Builder& WithPositionRelativeToWindow(bool set = true) {
			return WithToggleFlag(TDF_POSITION_RELATIVE_TO_WINDOW, set);
		}

		Builder& WithRtlLayout(bool set = true) {
			return WithToggleFlag(TDF_ALLOW_DIALOG_CANCELLATION, set);
		}

		Builder& WithCanBeMinimized(bool set = true) {
			return WithToggleFlag(TDF_CAN_BE_MINIMIZED, set);
		}

		Builder& WithSizeToContent(bool set = true) {
			return WithToggleFlag(TDF_SIZE_TO_CONTENT, set);
		}

		Builder& WithCommonButton(_TASKDIALOG_COMMON_BUTTON_FLAGS id, bool set = true) {
			m_dialog.m_dialogConfig.dwCommonButtons = (m_dialog.m_dialogConfig.dwCommonButtons & ~id) | (set ? id : 0U);
			return *this;
		}

		Builder& WithWindowTitle(StringOrResId s) {
			m_dialog.m_windowTitle = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithWindowTitleFormat(const StringOrResId& s, Args ...args) {
			return WithWindowTitle(std::format(GetResString(s), std::forward<Args>(args...)));
		}

		Builder& WithMainIcon(HIconOrRes s) {
			m_dialog.m_mainIcon = s;
			return *this;
		}

		Builder& WithMainInstruction(StringOrResId s) {
			m_dialog.m_mainInstruction = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithMainInstructionFormat(const StringOrResId& s, Args ...args) {
			return WithMainInstruction(std::format(GetResString(s), std::forward<Args>(args...)));
		}

		Builder& WithContent(StringOrResId s) {
			m_dialog.m_content = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithContentFormat(const StringOrResId& s, Args ...args) {
			return WithContent(std::format(GetResString(s), std::forward<Args>(args...)));
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

		Builder& WithButtonRemoved(int id) {
			m_dialog.m_buttons.erase(std::remove_if(m_dialog.m_buttons.begin(), m_dialog.m_buttons.end(), [id](const auto& r) { return r.Id == id; }), m_dialog.m_buttons.end());
			return *this;
		}

		Builder& WithButtonDefault(int id) {
			m_dialog.m_dialogConfig.nDefaultButton = id;
			return *this;
		}

		Builder& WithButtonCommandLinks(CommandLinkType type = CommandLinkType::Icon) {
			return WithToggleFlags(CommandLinkType::Internal_AllBits, type);
		}

		Builder& WithRadio(Button b) {
			if (!b.IdSet) {
				do {
					b.Id = m_idCounter++;
				} while (IdExists(b.Id));
				b.IdSet = true;
			}
			m_dialog.m_radios.emplace_back(std::move(b));
			return *this;
		}

		Builder& WithRadioRemoved(int id) {
			m_dialog.m_radios.erase(std::remove_if(m_dialog.m_radios.begin(), m_dialog.m_radios.end(), [id](const auto& r) { return r.Id == id; }), m_dialog.m_radios.end());
			return *this;
		}

		Builder& WithRadioDefault(int id) {
			m_dialog.m_dialogConfig.nDefaultRadioButton = id;
			return *this;
		}

		Builder& WithRadioNoDefault(bool set = true) {
			return WithToggleFlag(TDF_NO_DEFAULT_RADIO_BUTTON, set);
		}

		Builder& WithCheckbox(StringOrResId s) {
			m_dialog.m_checkbox = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithCheckboxFormat(const StringOrResId& s, Args ...args) {
			return WithCheckbox(std::format(GetResString(s), std::forward<Args>(args...)));
		}

		Builder& WithCheckboxDefault(bool set = true) {
			return WithToggleFlag(TDF_VERIFICATION_FLAG_CHECKED, set);
		}

		Builder& WithExpandedInformation(StringOrResId s) {
			m_dialog.m_expandedInformation = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithExpandedInformationFormat(const StringOrResId& s, Args ...args) {
			return WithExpandedInformation(std::format(GetResString(s), std::forward<Args>(args...)));
		}

		Builder& WithExpandedByDefault(bool set = true) {
			return WithToggleFlag(TDF_EXPANDED_BY_DEFAULT, set);
		}

		Builder& WithExpandFooterArea(bool set = true) {
			return WithToggleFlag(TDF_EXPAND_FOOTER_AREA, set);
		}

		Builder& WithExpandedControlText(StringOrResId s) {
			m_dialog.m_expandedControlText = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithExpandedControlTextFormat(const StringOrResId& s, Args ...args) {
			return WithExpandedControlText(std::format(GetResString(s), std::forward<Args>(args...)));
		}

		Builder& WithCollapsedControlText(StringOrResId s) {
			m_dialog.m_collapsedControlText = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithCollapsedControlTextFormat(const StringOrResId& s, Args ...args) {
			return WithCollapsedControlText(std::format(GetResString(s), std::forward<Args>(args...)));
		}

		Builder& WithFooterIcon(HIconOrRes s) {
			m_dialog.m_footerIcon = s;
			return *this;
		}

		Builder& WithFooter(StringOrResId s) {
			m_dialog.m_footer = std::move(s);
			return *this;
		}

		template<typename...Args>
		Builder& WithFooterFormat(const StringOrResId& s, Args ...args) {
			return WithFooter(std::format(GetResString(s), std::forward<Args>(args...)));
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

		std::wstring_view GetResString(const StringOrResId& s) const {
			if (s.StringRes) {
				const auto data = FindStringResourceEx(m_dialog.m_dialogConfig.hInstance, static_cast<UINT>(reinterpret_cast<size_t>(s.StringRes)), m_languageId);
				return {data + 1, static_cast<size_t>(*data)};
			} else
				return s.Text;
		}

		[[nodiscard]] bool IdExists(int id) const {
			return std::ranges::any_of(m_dialog.m_buttons, [id](const auto& r) { return r.Id == id; })
				|| std::ranges::any_of(m_dialog.m_radios, [id](const auto& r) { return r.Id == id; });
		}
	};
}
