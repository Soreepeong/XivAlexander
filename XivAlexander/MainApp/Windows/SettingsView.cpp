#include "pch.h"
#include "MainApp/Windows/SettingsView.h"
#include "MainApp/Windows/ConfigWindow.h"
#include "MainApp/Windows/ThemeColors.h"

#include "Utils/Win32/Resource.h"
#include "Utils/Win32/TaskDialogBuilder.h"

#include "Config.h"
#include "MainApp/App.h"
#include "MainApp/Features/LoginSessions.h"
#include "MainApp/Features/AudioResampler.h"
#include "MainApp/Features/NetworkTimingHandler.h"
#include <FontChanger.FixedSizeFont/image_glyph_files.h>

#include "MainApp/FontReplacement/FamilyPreview.h"
#include "MainApp/FontReplacement/GameFontNames.h"
#include "MainApp/FontReplacement/Presets.h"
#include "MainApp/Modding/NestedTtmp.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Modding/VirtualSqPacks.h"
#include "resource.h"
#include "XivAlexander.h"

namespace {
	constexpr auto PageClassName = L"XivAlexander::Window::SettingsView::Page";

	// The line between the tree and the page, as the host's menu bar has under it, in the middle of where it is
	// dragged from to resize them.
	constexpr int DividerSize = 1;
	constexpr int DividerGrabSize = 7;  // At 100% zoom.

	enum : UINT {
		WmRefreshRows = WM_APP + 1,
		WmRebuildPage,
		WmPopulateTree,
		WmRefreshTheme,
		WmListenToCooldowns,
		WmCooldown,
		WmTtmpSetsChanged,
		WmSelectTtmpPack,
		WmFontPreviewDrawn,
	};

	// The controls of the n-th row of the page have the IDs FirstRowControlId + n * ControlsPerRow + RowPart.
	constexpr int FirstRowControlId = 1000;
	constexpr int FirstTabControlId = 900;  // Of the buttons of the page's tabs, below its rows'.
	constexpr int ControlsPerRow = 8;

	enum RowPart : int {
		PartLabel,
		PartControl,
		PartButton1,
		PartButton2,
		PartDescription,
		PartSecondary,
		PartButton3,
		PartButton4,
	};

	constexpr auto ContainerClassName = L"XivAlexander::Window::SettingsView";
	constexpr auto StatusClassName = L"XivAlexander::Window::SettingsView::Status";
	constexpr auto FontPreviewClassName = L"XivAlexander::Window::SettingsView::FontPreview";

	// How long a font family's preview waits for changes to stop before it is drawn: making the faces can take a while.
	constexpr auto FontPreviewDelay = std::chrono::milliseconds(300);

	// Latin, Japanese and Korean, so that what the family's sources lack shows.
	constexpr auto DefaultFontPreviewText = L"The quick brown fox jumps over the lazy dog. 0123456789 いろはにほへと ちりぬるを 漢字とカタカナ 다람쥐 헌 쳇바퀴에 타고파";

	// Meidinger draws the numbers flying over battles: digits and a few signs only. Its sample text is its own.
	constexpr auto MeidingerFamily = "Meidinger";
	constexpr auto MeidingerPreviewText = L"0123456789?!%+-./";

	// Which sample text a family's page shows and keeps: Meidinger's, or the one the others share.
	std::string FontPreviewTextKey(const std::string& family) {
		return family == MeidingerFamily ? family : std::string();
	}

	// The family the font replacement's page previews the edge with: that of most of the UI's text.
	constexpr auto EdgePreviewFamily = "AXIS";

	// The sizes a preview can be drawn at: those the replacement draws (FontReplacer::GetDrawnPx).
	constexpr float MinFontPreviewSize = 4;
	constexpr float MaxFontPreviewSize = 255;

	// Colors of text in the game, for the previews: a text color and an edge color the game's strings set together with
	// <colortype(n)> and <edgecolortype(m)> (rows n and m of UIColor, dark theme), and a background to see both on. The
	// pairs are the most used in the game's sheets' strings as of game version 2026.09.15.0000.0000, counted by
	// scratch\local\uicolor\uicolor_stats.cpp. 571/572, white on a black edge, is left out as the same as Default;
	// those setting a text color only (547, an unseen speaker's lines) keep the edge of the text around them.
	struct FontPreviewColorPreset {
		UINT LabelId;
		COLORREF Text;
		COLORREF Edge;
		COLORREF Background;
	};
	constexpr FontPreviewColorPreset FontPreviewColorPresets[]{
		// The game's text without color macros: white, with a dark edge.
		{IDS_SETTINGS_FONT_COLORS_DEFAULT, RGB(0xFF, 0xFF, 0xFF), RGB(0x00, 0x00, 0x00), RGB(0x60, 0x60, 0x60)},
		// 504/505: "Duration:", "Potency:" and other labels in tooltips.
		{IDS_SETTINGS_FONT_COLORS_TOOLTIPLABEL, RGB(0x00, 0xCC, 0x22), RGB(0x00, 0x00, 0x00), RGB(0x60, 0x60, 0x60)},
		// 500/501: names of actions and terms in descriptions, and headings.
		{IDS_SETTINGS_FONT_COLORS_ACTIONNAME, RGB(0xFF, 0x7B, 0x1A), RGB(0x00, 0x00, 0x00), RGB(0x60, 0x60, 0x60)},
		// 506/507: names of statuses in descriptions, and notes.
		{IDS_SETTINGS_FONT_COLORS_STATUSNAME, RGB(0xFF, 0xFF, 0x66), RGB(0x00, 0x00, 0x00), RGB(0x60, 0x60, 0x60)},
		// 508/509: warnings, and confirmations of what can't be undone.
		{IDS_SETTINGS_FONT_COLORS_WARNING, RGB(0xFF, 0x80, 0x80), RGB(0x00, 0x00, 0x00), RGB(0x60, 0x60, 0x60)},
		// 533/534: notices such as "This quest has been adjusted...", pale on a red edge.
		{IDS_SETTINGS_FONT_COLORS_NOTICE, RGB(0xFF, 0xC8, 0xC8), RGB(0xAE, 0x00, 0x00), RGB(0x30, 0x30, 0x30)},
		// 582/581: places and things highlighted in some quests, orange on a purple edge.
		{IDS_SETTINGS_FONT_COLORS_QUESTHIGHLIGHT, RGB(0xFF, 0xA0, 0x00), RGB(0x40, 0x00, 0x80), RGB(0x30, 0x30, 0x30)},
		// 7/3: the text of content tutorials, black on a grey edge over a light page.
		{IDS_SETTINGS_FONT_COLORS_TUTORIAL, RGB(0x00, 0x00, 0x00), RGB(0xA0, 0xA0, 0xA0), RGB(0xE8, 0xE0, 0xD0)},
	};

	std::wstring GetText(HWND hWnd) {
		std::wstring text(static_cast<size_t>(GetWindowTextLengthW(hWnd)) + 1, L'\0');
		text.resize(GetWindowTextW(hWnd, text.data(), static_cast<int>(text.size())));
		return text;
	}

	std::wstring Trim(std::wstring s) {
		const auto first = s.find_first_not_of(L" \t");
		if (first == std::wstring::npos)
			return {};
		return s.substr(first, s.find_last_not_of(L" \t") - first + 1);
	}

	/// Turns a menu item's text into a label: without the shortcut after the tab, and without the access key, whether
	/// marked in place ("&Enable") or after the text ("有効(&E)").
	std::wstring CleanMenuText(std::wstring s) {
		if (const auto tab = s.find(L'\t'); tab != std::wstring::npos)
			s.resize(tab);
		static const std::wregex TrailingAccessKey(LR"(\(&[^)]\))");
		s = std::regex_replace(s, TrailingAccessKey, L"");

		std::wstring res;
		for (size_t i = 0; i < s.size(); ++i) {
			if (s[i] == L'&') {
				if (i + 1 < s.size() && s[i + 1] == L'&')
					res += s[++i];
				continue;
			}
			res += s[i];
		}
		return Trim(std::move(res));
	}

	/// Gets the client GUID of the file dialogs for a purpose, so that each purpose's dialogs remember their own last folder
	/// instead of sharing one. It is made from the purpose's name (FNV-1a, twice with different starting values), so that it
	/// stays the same from build to build.
	GUID DialogGuid(std::string_view purpose) {
		const auto hash = [purpose](uint64_t value) {
			for (const auto c : std::string_view("XivAlexander.SettingsView.FileDialog."))
				value = (value ^ static_cast<uint8_t>(c)) * 0x100000001b3ULL;
			for (const auto c : purpose)
				value = (value ^ static_cast<uint8_t>(c)) * 0x100000001b3ULL;
			return value;
		};
		const auto high = hash(0xcbf29ce484222325ULL);
		const auto low = hash(0x84222325cbf29ce4ULL);
		GUID guid{
			.Data1 = static_cast<uint32_t>(high >> 32),
			.Data2 = static_cast<uint16_t>(high >> 16),
			// Marked as made by a method of its own (RFC 9562's version 8 and variant), so it can't be taken for another kind.
			.Data3 = static_cast<uint16_t>((high & 0x0FFF) | 0x8000),
		};
		for (size_t i = 0; i < 8; ++i)
			guid.Data4[i] = static_cast<uint8_t>(low >> (56 - i * 8));
		guid.Data4[0] = static_cast<uint8_t>((guid.Data4[0] & 0x3F) | 0x80);
		return guid;
	}

	/// What PickPath asks for.
	enum class PickKind {
		Folder,
		Dll,
		Preset,  // A font preset (.json).
	};

	/// Asks for a folder or a file, starting in initial if it is a folder; purpose tells the dialog's last folder apart
	/// from other dialogs' (DialogGuid).
	std::optional<std::filesystem::path> PickPath(HWND hOwner, const std::filesystem::path& initial, PickKind kind, std::string_view purpose) {
		try {
			IFileOpenDialogPtr pDialog;
			DWORD dwFlags;
			Utils::Win32::Error::ThrowIfFailed(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetClientGuid(DialogGuid(purpose)));
			Utils::Win32::Error::ThrowIfFailed(pDialog->GetOptions(&dwFlags));
			Utils::Win32::Error::ThrowIfFailed(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | (kind == PickKind::Folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST)));
			if (kind == PickKind::Dll) {
				static constexpr COMDLG_FILTERSPEC FileTypes[]{
					{L"*.dll", L"*.dll"},
					{L"*.*", L"*.*"},
				};
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(static_cast<UINT>(std::size(FileTypes)), FileTypes));
			} else if (kind == PickKind::Preset) {
				static constexpr COMDLG_FILTERSPEC FileTypes[]{
					{L"*.json", L"*.json"},
					{L"*.*", L"*.*"},
				};
				Utils::Win32::Error::ThrowIfFailed(pDialog->SetFileTypes(static_cast<UINT>(std::size(FileTypes)), FileTypes));
			}
			if (!initial.empty() && is_directory(initial)) {
				IShellItemPtr pFolder;
				// The presets' folder is only where it starts the first time; after that, where one was last picked from.
				if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&pFolder)))) {
					if (kind == PickKind::Preset)
						pDialog->SetDefaultFolder(pFolder);
					else
						pDialog->SetFolder(pFolder);
				}
			}
			Utils::Win32::Error::ThrowIfFailed(pDialog->Show(hOwner), true);

			IShellItemPtr pResult;
			PWSTR pszPath;
			Utils::Win32::Error::ThrowIfFailed(pDialog->GetResult(&pResult));
			Utils::Win32::Error::ThrowIfFailed(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath));
			if (!pszPath)
				return std::nullopt;
			const auto freePath = xivres::util::on_dtor([pszPath] { CoTaskMemFree(pszPath); });
			return std::filesystem::path(pszPath);

		} catch (const Utils::Win32::CancelledError&) {
			return std::nullopt;

		} catch (const std::exception& e) {
			Dll::MessageBoxF(hOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			return std::nullopt;
		}
	}

	/// Opens a folder in Explorer, or the folder of a file with the file selected.
	void OpenInExplorer(HWND hOwner, const std::filesystem::path& path) {
		std::error_code ec;
		if (is_directory(path, ec)) {
			ShellExecuteW(hOwner, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
		} else if (exists(path, ec)) {
			if (const auto pidl = ILCreateFromPathW(path.c_str())) {
				SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
				ILFree(pidl);
			}
		} else {
			MessageBeep(MB_ICONWARNING);
		}
	}

	template<typename T>
	std::wstring FormatNumber(T value) {
		if constexpr (std::is_floating_point_v<T>)
			return std::format(L"{:g}", value);
		else
			return std::format(L"{}", value);
	}

	/// Parses the whole of text as a T; nullopt if it isn't one, or is out of its range.
	template<typename T>
	std::optional<T> ParseNumber(const std::wstring& text) {
		const auto s = Trim(text);
		if (s.empty())
			return std::nullopt;

		wchar_t* end{};
		errno = 0;
		T value;
		if constexpr (std::is_floating_point_v<T>) {
			value = static_cast<T>(std::wcstod(s.c_str(), &end));
		} else if constexpr (std::is_signed_v<T>) {
			const auto v = std::wcstoll(s.c_str(), &end, 10);
			if (v < std::numeric_limits<T>::min() || v > std::numeric_limits<T>::max())
				return std::nullopt;
			value = static_cast<T>(v);
		} else {
			if (s.find(L'-') != std::wstring::npos)
				return std::nullopt;
			const auto v = std::wcstoull(s.c_str(), &end, 10);
			if (v > std::numeric_limits<T>::max())
				return std::nullopt;
			value = static_cast<T>(v);
		}
		if (errno == ERANGE || end != s.c_str() + s.size())
			return std::nullopt;
		return value;
	}

	template<typename T>
	std::wstring EnumName(T value) {
		return xivres::util::unicode::convert<std::wstring>(nlohmann::json(value).get<std::string>());
	}
}

// DirectWrite, for the names of the system's font families.
_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily, __uuidof(IDWriteFontFamily));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalizedStrings, __uuidof(IDWriteLocalizedStrings));

namespace {
	std::wstring Wide(const std::string& s) {
		return xivres::util::unicode::convert<std::wstring>(s);
	}

	std::string Utf8(const std::wstring& s) {
		return xivres::util::unicode::convert<std::string>(s);
	}

	bool IsValidRegex(const std::string& pattern) {
		try {
			srell::u8cregex(pattern, srell::regex_constants::icase);
			return true;
		} catch (const std::exception&) {
			return false;
		}
	}

	/// Changes an element of a list of a config item; nothing if it isn't there.
	template<typename T, typename TFn>
	void EditAt(XivAlexander::ConfigItem<std::vector<T>>& item, size_t index, TFn&& fn) {
		auto values = item.Value();
		if (index >= values.size())
			return;
		fn(values[index]);
		item = std::move(values);
	}

	/// Moves an element of a list, to before the one now at the index to; as GridView::Source::MoveRow.
	template<typename T>
	void MoveWithin(std::vector<T>& values, size_t from, size_t to) {
		auto value = std::move(values[from]);
		values.erase(values.begin() + static_cast<ptrdiff_t>(from));
		values.insert(values.begin() + static_cast<ptrdiff_t>(to > from ? to - 1 : to), std::move(value));
	}

	template<typename T>
	std::function<void(size_t, size_t)> MoveIn(XivAlexander::ConfigItem<std::vector<T>>& item) {
		return [&item](size_t from, size_t to) {
			auto values = item.Value();
			MoveWithin(values, from, to);
			item = std::move(values);
		};
	}

	/// Adds an element to a list, and edits a cell of it.
	template<typename T>
	std::function<void()> AddTo(XivAlexander::ConfigItem<std::vector<T>>& item, XivAlexander::Apps::MainApp::Window::GridView& grid, T value, size_t column) {
		return [&item, &grid, value = std::move(value), column] {
			auto values = item.Value();
			values.push_back(value);
			item = std::move(values);
			grid.Refresh();
			grid.BeginEdit(item.Value().size() - 1, column);
		};
	}

	/// Removes the selected element of a list.
	template<typename T>
	std::function<void()> RemoveFrom(XivAlexander::ConfigItem<std::vector<T>>& item, XivAlexander::Apps::MainApp::Window::GridView& grid) {
		return [&item, &grid] {
			const auto selected = grid.GetSelectedRow();
			if (!selected || *selected >= item.Value().size())
				return;
			auto values = item.Value();
			values.erase(values.begin() + static_cast<ptrdiff_t>(*selected));
			item = std::move(values);
			grid.Refresh();
			if (!item.Value().empty())
				grid.Select(std::min(*selected, item.Value().size() - 1));
		};
	}

	// DirectWrite's names of weights, stretches, and styles, after their values.
	const std::vector<std::pair<int, const wchar_t*>> FontWeights{
		{100, L"Thin"}, {200, L"Extra Light"}, {300, L"Light"}, {350, L"Semi Light"}, {400, L"Regular"}, {500, L"Medium"},
		{600, L"Semi Bold"}, {700, L"Bold"}, {800, L"Extra Bold"}, {900, L"Black"}, {950, L"Extra Black"},
	};
	const std::vector<std::pair<int, const wchar_t*>> FontStretches{
		{1, L"Ultra Condensed"}, {2, L"Extra Condensed"}, {3, L"Condensed"}, {4, L"Semi Condensed"}, {5, L"Normal"},
		{6, L"Semi Expanded"}, {7, L"Expanded"}, {8, L"Extra Expanded"}, {9, L"Ultra Expanded"},
	};
	const std::vector<std::pair<int, const wchar_t*>> FontStyles{
		{0, L"Normal"}, {1, L"Oblique"}, {2, L"Italic"},
	};

	std::wstring NamedValue(const std::vector<std::pair<int, const wchar_t*>>& names, int value) {
		for (const auto& [v, name] : names) {
			if (v == value)
				return std::format(L"{} {}", v, name);
		}
		return std::format(L"{}", value);
	}

	/// For the text of a button, which would take an ampersand as the start of a shortcut.
	// The box of a label being edited in the tree takes Enter and Esc, which the window, as a dialog, would otherwise take
	// as its OK and Cancel before the box sees them.
	constexpr UINT_PTR LabelEditSubclassId = 1;
	LRESULT CALLBACK LabelEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
		if (uMsg == WM_GETDLGCODE)
			return DefSubclassProc(hWnd, uMsg, wParam, lParam) | DLGC_WANTALLKEYS;
		if (uMsg == WM_NCDESTROY)
			RemoveWindowSubclass(hWnd, LabelEditSubclassProc, LabelEditSubclassId);
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	std::wstring EscapeMnemonics(std::wstring_view text) {
		std::wstring res;
		for (const auto c : text) {
			if (c == L'&')
				res += L'&';
			res += c;
		}
		return res;
	}

	std::vector<std::wstring> NamedValues(const std::vector<std::pair<int, const wchar_t*>>& names) {
		std::vector<std::wstring> res;
		for (const auto& [v, name] : names)
			res.emplace_back(std::format(L"{} {}", v, name));
		return res;
	}
}

struct XivAlexander::Apps::MainApp::Window::SettingsView::Row {
	enum class RowType {
		Unsupported,  // Edited in the runtime configuration editor.
		Check,
		Edit,
		Combo,
		ComboEdit,  // Choices, or a value typed in.
		PathEdit,
		PathList,
		Heading,  // Of an inline group; no item.
		Note,  // Text only; no item.
		Action,  // Buttons; no item.
		Value,  // A label and a value shown; no item.
		Grid,  // A grid of a list; no item.
		RichText,  // Text that can be selected, with its links followed, shortened unless expanded; no item.
		Tabs,  // Buttons choosing which of the rows after it are shown; no item.
		Slider,  // A slider, and the number it sets; no item.
		Preview,  // A font family's preview, drawn by the control; no item.
	};

	ConfigItemBase* Item{};
	RowType Type = RowType::Unsupported;
	std::vector<std::wstring> ComboLabels;
	bool PickFiles = false;

	HWND Label{};
	HWND Control{};
	HWND Button1{};
	HWND Button2{};
	HWND Button3{};
	HWND Button4{};
	HWND Description{};
	HWND Secondary{};  // The number of a slider.

	std::unique_ptr<XivAlexander::Apps::MainApp::Window::GridView> Grid;
	int Height = 0;  // Of a grid, at 100% zoom.
	int FlowGroup = 0;  // Of a check row without a description: side by side with the rows before it of the same group.
	// Of an edit or combo row without a description: on one line with the rows next to it of the same group, each label
	// as wide as its text and each control LineWidth wide (at 100% zoom), but the last, which takes the rest of the line.
	int LineGroup = 0;
	int LineWidth = 0;
	bool CommitOnChange = false;  // Of an edit row: as it is typed into, rather than when left.
	bool Bold = false;  // Of a check row that is a group's heading.
	bool Expanded = false;  // Of a rich text row: all of it shown.
	bool LinkButton = false;  // Button1 is drawn as a link.
	std::function<void(POINT ptScreen)> ContextMenu;  // A right click on the control.

	std::vector<HWND> Tabs;  // Of a tabs row: its buttons,
	int ActiveTab = 0;  // and which is chosen.
	const Row* TabOwner{};  // Of a row in a tab: the tabs row,
	int TabIndex = -1;  // and its tab.

	// Shows the item's value in the controls.
	std::function<void()> Refresh;
	// Sets the item to what is in the controls.
	std::function<void()> Commit;
	// A click on the label, the control, or a button (RowPart).
	std::function<void(int)> Click;
};

struct XivAlexander::Apps::MainApp::Window::SettingsView::FontPreview {
	std::string Family;
	std::vector<std::pair<std::string, float>> Faces;  // Of the family, with their sizes (GameFontNames::FacesOf).
	HWND Control{};  // Of its row, which draws Image.
	uint64_t Generation = 0;  // Of the latest request; 0 before the first.
	bool Reload = true;  // The page's first request makes the faces anew: their files may have changed since.
	SIZE Requested{};  // The size of the latest request.
	std::optional<FontReplacement::FamilyPreview::Image> Image;
};

namespace {
	template<typename TRow>
	bool BindBool(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<bool>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::Check;
		row.Refresh = [&row, item] { Button_SetCheck(row.Control, item->Value() ? BST_CHECKED : BST_UNCHECKED); };
		row.Click = [&row, item](int part) {
			if (part == PartControl)
				*item = Button_GetCheck(row.Control) == BST_CHECKED;
			else if (part == PartLabel)
				item->Toggle();
		};
		return true;
	}

	template<typename T, typename TRow>
	bool BindNumber(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<T>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::Edit;
		row.Refresh = [&row, item] { SetWindowTextW(row.Control, FormatNumber(item->Value()).c_str()); };
		row.Commit = [&row, item] {
			// What can't be read goes back to the current value.
			if (const auto value = ParseNumber<T>(GetText(row.Control)))
				*item = *value;
			row.Refresh();
		};
		return true;
	}

	/// For an opcode: shown in hexadecimal, as the opcode configuration has them; read in any base C accepts.
	template<typename TRow>
	bool BindHex16(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<uint16_t>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::Edit;
		row.Refresh = [&row, item] { SetWindowTextW(row.Control, std::format(L"0x{:04x}", item->Value()).c_str()); };
		row.Commit = [&row, item] {
			const auto text = Trim(GetText(row.Control));
			wchar_t* end{};
			errno = 0;
			if (const auto value = std::wcstoul(text.c_str(), &end, 0); !text.empty() && errno != ERANGE && end == text.c_str() + text.size() && value <= 0xFFFF)
				*item = static_cast<uint16_t>(value);
			row.Refresh();
		};
		return true;
	}

	template<typename TRow>
	bool BindString(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<std::string>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::Edit;
		row.Refresh = [&row, item] { SetWindowTextW(row.Control, xivres::util::unicode::convert<std::wstring>(item->Value()).c_str()); };
		row.Commit = [&row, item] {
			*item = xivres::util::unicode::convert<std::string>(GetText(row.Control));
			row.Refresh();
		};
		return true;
	}

	template<typename TRow>
	bool BindPath(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<std::filesystem::path>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::PathEdit;
		row.Refresh = [&row, item] { SetWindowTextW(row.Control, item->Value().c_str()); };
		row.Commit = [&row, item] {
			*item = std::filesystem::path(Trim(GetText(row.Control)));
			row.Refresh();
		};
		row.Click = [&row, item](int part) {
			if (part != PartButton1)
				return;
			if (const auto path = PickPath(GetAncestor(row.Control, GA_ROOT), item->Value(), PickKind::Folder, std::format("Path.{}", item->Name)))
				*item = *path;
		};
		return true;
	}

	template<typename TRow>
	bool BindPathList(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<std::vector<std::filesystem::path>>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::PathList;
		row.Refresh = [&row, item] {
			const auto selected = ListBox_GetCurSel(row.Control);
			ListBox_ResetContent(row.Control);
			for (const auto& path : item->Value())
				ListBox_AddString(row.Control, path.c_str());
			ListBox_SetCurSel(row.Control, std::min(selected, ListBox_GetCount(row.Control) - 1));
		};
		row.Click = [&row, item](int part) {
			const auto selected = ListBox_GetCurSel(row.Control);
			const auto hasSelection = selected >= 0 && static_cast<size_t>(selected) < item->Value().size();
			if (part == PartButton1) {
				// The chain-loaded DLLs share where they are picked from.
				const auto purpose = row.PickFiles ? std::string("ChainLoadDll") : std::format("PathList.{}", item->Name);
				if (const auto path = PickPath(GetAncestor(row.Control, GA_ROOT), {}, row.PickFiles ? PickKind::Dll : PickKind::Folder, purpose)) {
					auto paths = item->Value();
					paths.push_back(*path);
					*item = std::move(paths);
				}
			} else if (part == PartButton2) {
				if (!hasSelection)
					return;
				auto paths = item->Value();
				paths.erase(paths.begin() + selected);
				*item = std::move(paths);
			} else if (part == PartControl) {
				// Double-clicked.
				if (hasSelection)
					OpenInExplorer(GetAncestor(row.Control, GA_ROOT), item->Value()[selected]);
			}
		};
		return true;
	}

	/// For a number with suggested values; makeChoices gives them, with their labels.
	template<typename T, typename TRow, typename TMakeChoices>
	bool BindNumberCombo(TRow& row, TMakeChoices&& makeChoices) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<T>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::ComboEdit;
		const auto choices = std::make_shared<std::vector<std::pair<T, std::wstring>>>(makeChoices());
		for (const auto& label : *choices | std::views::values)
			row.ComboLabels.push_back(label);
		row.Refresh = [&row, item, choices] {
			if (const auto it = std::ranges::find(*choices, item->Value(), &std::pair<T, std::wstring>::first); it != choices->end()) {
				ComboBox_SetCurSel(row.Control, static_cast<int>(it - choices->begin()));
			} else {
				ComboBox_SetCurSel(row.Control, -1);
				SetWindowTextW(row.Control, FormatNumber(item->Value()).c_str());
			}
		};
		row.Commit = [&row, item, choices] {
			// A choice's label stands for its value; anything else is read as a number, or goes back to the current value.
			const auto text = Trim(GetText(row.Control));
			if (const auto it = std::ranges::find(*choices, text, &std::pair<T, std::wstring>::second); it != choices->end())
				*item = it->first;
			else if (const auto value = ParseNumber<T>(text))
				*item = *value;
			row.Refresh();
		};
		row.Click = [&row, item, choices](int part) {
			// A choice was picked from the list.
			if (part != PartControl)
				return;
			if (const auto selected = ComboBox_GetCurSel(row.Control); selected >= 0 && static_cast<size_t>(selected) < choices->size())
				*item = (*choices)[selected].first;
		};
		return true;
	}

	/// For an item of enum type T; makeChoices gives the values to choose from, with their labels.
	template<typename T, typename TRow, typename TMakeChoices>
	bool BindEnum(TRow& row, TMakeChoices&& makeChoices) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<T>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::Combo;
		const auto choices = std::make_shared<std::vector<std::pair<T, std::wstring>>>(makeChoices());
		for (const auto& label : *choices | std::views::values)
			row.ComboLabels.push_back(label);
		row.Refresh = [&row, item, choices] {
			const auto it = std::ranges::find(*choices, item->Value(), &std::pair<T, std::wstring>::first);
			ComboBox_SetCurSel(row.Control, it == choices->end() ? -1 : static_cast<int>(it - choices->begin()));
		};
		row.Commit = [&row, item, choices] {
			if (const auto selected = ComboBox_GetCurSel(row.Control); selected >= 0 && static_cast<size_t>(selected) < choices->size())
				*item = (*choices)[selected].first;
		};
		return true;
	}

	/// Lists every value of an enum serialized by name, labelled by that name.
	template<typename T>
	std::vector<std::pair<T, std::wstring>> NamedChoices(std::initializer_list<T> values) {
		std::vector<std::pair<T, std::wstring>> res;
		for (const auto v : values)
			res.emplace_back(v, EnumName(v));
		return res;
	}
}

namespace {
	/// Whether the row takes typed text, and has the focus: the edit of an editable combo box is its child.
	template<typename TRow>
	bool IsTypingInto(const TRow& row, HWND hFocus) {
		switch (row.Type) {
			case TRow::RowType::Edit:
			case TRow::RowType::PathEdit:
				return hFocus && hFocus == row.Control;
			case TRow::RowType::ComboEdit:
				return hFocus && (hFocus == row.Control || GetParent(hFocus) == row.Control);
			case TRow::RowType::Slider:
				return hFocus && hFocus == row.Secondary;
			default:
				return false;
		}
	}
}

XivAlexander::Apps::MainApp::Window::SettingsView::SettingsView(App& app, HWND hWndParent, HWND hWndOwner, std::function<double()> getZoom, std::function<bool()> isDarkMode, std::function<void(HDC, const RECT&)> paintStatus, RestartChoices restartChoices, std::function<void(Features::Modding::NestedTtmp& folder, UINT commandId)> batchTtmp)
	: m_app(app)
	, m_config(Config::Acquire())
	, m_hWndOwner(hWndOwner)
	, m_getZoom(std::move(getZoom))
	, m_isDarkMode(std::move(isDarkMode))
	, m_paintStatus(std::move(paintStatus))
	, m_restartChoices(std::move(restartChoices))
	, m_batchTtmp(std::move(batchTtmp)) {
	InitializeItemInfo();

	// The windows made here pass their messages to this object, which they have as GWLP_USERDATA.
	const auto registerClass = [](LPCWSTR className, WNDPROC wndProc) {
		const WNDCLASSEXW wcex{
			.cbSize = sizeof(WNDCLASSEXW),
			.lpfnWndProc = wndProc,
			.hInstance = Dll::Module(),
			.hCursor = LoadCursorW(nullptr, IDC_ARROW),
			.lpszClassName = className,
		};
		if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			throw Utils::Win32::Error("RegisterClassExW");
	};
	registerClass(ContainerClassName, [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		if (const auto self = reinterpret_cast<SettingsView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
			return self->ContainerProc(hwnd, uMsg, wParam, lParam);
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	});
	registerClass(PageClassName, [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		if (const auto self = reinterpret_cast<SettingsView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
			return self->PageProc(hwnd, uMsg, wParam, lParam);
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	});
	registerClass(StatusClassName, [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		if (const auto self = reinterpret_cast<SettingsView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
			return self->StatusProc(hwnd, uMsg, wParam, lParam);
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	});
	registerClass(FontPreviewClassName, [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		const auto self = reinterpret_cast<SettingsView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (uMsg == WM_ERASEBKGND)
			return 1;
		if (uMsg == WM_PAINT && self) {
			self->PaintFontPreview(hwnd);
			return 0;
		}
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	});

	const auto create = [this](HWND hParent, DWORD exStyle, LPCWSTR className, DWORD style) {
		const auto hWnd = CreateWindowExW(exStyle, className, L"", WS_CHILD | style, 0, 0, 0, 0, hParent, nullptr, Dll::Module(), nullptr);
		if (!hWnd)
			throw Utils::Win32::Error("CreateWindowExW");
		return hWnd;
	};
	m_hWnd = create(hWndParent, WS_EX_CONTROLPARENT, ContainerClassName, WS_VISIBLE | WS_CLIPCHILDREN);
	SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	m_hTree = create(m_hWnd, 0, WC_TREEVIEWW, WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_EDITLABELS);
	TreeView_SetExtendedStyle(m_hTree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

	m_hPage = create(m_hWnd, WS_EX_CONTROLPARENT, PageClassName, WS_VSCROLL | WS_CLIPCHILDREN);
	SetWindowLongPtrW(m_hPage, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	m_hStatus = create(m_hWnd, 0, StatusClassName, WS_VISIBLE);
	SetWindowLongPtrW(m_hStatus, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	// The labels of version-sensitive items are marked while those features are off for this session.
	m_cleanup += m_config->Runtime.OnVersionSensitiveFeaturesAllowedChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });

	// The cooldowns, from the latency helper, which comes and goes with its setting: after the app has made or removed it.
	m_cleanup += m_config->Runtime.NetworkTiming.Enabled.AddAndCallOnChange([this] { PostMessageW(m_hWnd, WmListenToCooldowns, 0, 0); });

	// The ModPacks are loaded on another thread, after this; their folders are in the tree.
	m_cleanup += m_app.GetResourceOverrider().OnVirtualSqPacksInitialized([this] { PostMessageW(m_hWnd, WmTtmpSetsChanged, 0, 0); });
	ListenToTtmps();

	RefreshTheme();
	UpdateFont(GetZoom());
	PopulateTree();
}

XivAlexander::Apps::MainApp::Window::SettingsView::~SettingsView() {
	// Waits for a preview being drawn, which then posts to nothing.
	m_fontPreviewer.reset();
	m_rowCleanup.clear();
	m_cleanup.clear();
	if (IsWindow(m_hWnd))
		DestroyWindow(m_hWnd);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CommitTypedText() {
	const auto hFocus = GetFocus();
	for (const auto& row : m_rows) {
		if (row->Grid)
			row->Grid->CommitEdit();
		else if (row->Commit && IsTypingInto(*row, hFocus))
			row->Commit();
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::InvalidateStatus() const {
	if (IsWindowVisible(m_hStatus))
		InvalidateRect(m_hStatus, nullptr, FALSE);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::InitializeItemInfo() {
	auto& rt = m_config->Runtime;
	const auto label = [this](const ConfigItemBase& item, UINT labelId, UINT descriptionId = 0) {
		auto& info = m_itemInfo[&item];
		info.LabelId = labelId;
		info.DescriptionId = descriptionId;
	};
	const auto menu = [this](const ConfigItemBase& item, UINT menuId, UINT commandId) {
		auto& info = m_itemInfo[&item];
		info.MenuId = menuId;
		info.MenuCommand = commandId;
	};

	label(rt.Ui, IDS_SETTINGS_GROUP_UI);
	label(rt.Ui.MainWindow, IDS_SETTINGS_GROUP_UI_MAINWINDOW);
	label(rt.Ui.LogWindow, IDS_SETTINGS_GROUP_UI_LOGWINDOW);
	label(rt.Ui.ConfigWindow, IDS_SETTINGS_GROUP_UI_CONFIGWINDOW);
	label(rt.GameWindow, IDS_SETTINGS_GROUP_GAMEWINDOW);
	label(rt.NetworkTiming, IDS_SETTINGS_GROUP_NETWORKTIMING);
	label(rt.Socket, IDS_SETTINGS_GROUP_SOCKET);
	label(rt.FramerateControl, IDS_SETTINGS_GROUP_FRAMERATECONTROL);
	label(rt.FramerateControl.Lock, IDS_SETTINGS_GROUP_FRAMERATECONTROL_LOCK);
	label(rt.Opcodes, IDS_SETTINGS_GROUP_OPCODES);
	label(rt.Launch, IDS_SETTINGS_GROUP_LAUNCH);
	label(rt.ChainLoad, IDS_SETTINGS_GROUP_CHAINLOAD);
	label(rt.Modding, IDS_SETTINGS_GROUP_MODDING);
	label(rt.Modding.Languages, IDS_SETTINGS_GROUP_MODDING_LANGUAGES);
	label(rt.Modding.Ttmp, IDS_SETTINGS_GROUP_MODDING_TTMP);
	label(rt.Modding.Logging, IDS_SETTINGS_GROUP_MODDING_LOGGING);
	label(rt.Audio, IDS_SETTINGS_GROUP_AUDIO);
	label(rt.Modding.MuteVoice, IDS_SETTINGS_GROUP_AUDIO_MUTEVOICE);
	label(rt.Audio.SoxrResampler, IDS_SETTINGS_GROUP_AUDIO_SOXR);
	label(rt.Audio.SoxrResampler.Filter, IDS_SETTINGS_GROUP_AUDIO_SOXR_FILTER);
	label(rt.CrowdFix, IDS_SETTINGS_GROUP_CROWDFIX);
	label(rt.CrowdFix.Fixes, IDS_SETTINGS_GROUP_CROWDFIX_FIXES);
	label(rt.FontReplacement, IDS_SETTINGS_GROUP_FONTREPLACEMENT);
	label(rt.FontReplacement.Faces, IDS_SETTINGS_GROUP_FONTREPLACEMENT_FACES);

	// Items that have a menu command are labelled as it is.
	menu(rt.Ui.MainWindow.Show, IDR_TRAY_MENU, ID_FILE_SHOWCONTROLWINDOW);
	menu(rt.Ui.MainWindow.AlwaysOnTop, IDR_TRAY_MENU, ID_VIEW_ALWAYSONTOP);
	menu(rt.Ui.MainWindow.HideOnMinimize, IDR_TRAY_MENU, ID_VIEW_HIDEONMINIMIZE);
	menu(rt.Ui.LogWindow.Show, IDR_TRAY_MENU, ID_FILE_SHOWLOGGINGWINDOW);
	menu(rt.Ui.LogWindow.AlwaysOnTop, IDR_LOG_MENU, ID_VIEW_ALWAYSONTOP);
	menu(rt.Ui.LogWindow.UseWordWrap, IDR_LOG_MENU, ID_VIEW_USEWORDWRAP);
	menu(rt.Ui.LogWindow.UseMonospaceFont, IDR_LOG_MENU, ID_VIEW_USEMONOSPACEDFONT);
	menu(rt.Ui.ConfigWindow.UseWordWrap, IDR_CONFIG_EDITOR_MENU, ID_VIEW_USEWORDWRAP);
	menu(rt.GameWindow.AlwaysOnTop, IDR_TRAY_MENU, ID_VIEW_ALWAYSONTOPGAME);
	menu(rt.NetworkTiming.Enabled, IDR_TRAY_MENU, ID_NETWORK_HIGHLATENCYMITIGATION_ENABLE);
	menu(rt.NetworkTiming.UseHighLatencyMitigationLogging, IDR_TRAY_MENU, ID_NETWORK_HIGHLATENCYMITIGATION_USELOGGING);
	menu(rt.NetworkTiming.UseHighLatencyMitigationPreviewMode, IDR_TRAY_MENU, ID_NETWORK_HIGHLATENCYMITIGATION_PREVIEWMODE);
	menu(rt.Socket.ReducePacketDelay, IDR_TRAY_MENU, ID_NETWORK_REDUCEPACKETDELAY);
	menu(rt.Socket.TakeOverLoopbackAddresses, IDR_TRAY_MENU, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERLOOPBACKADDRESSES);
	menu(rt.Socket.TakeOverPrivateAddresses, IDR_TRAY_MENU, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERPRIVATEADDRESSES);
	menu(rt.Socket.TakeOverAllAddresses, IDR_TRAY_MENU, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLADDRESSES);
	menu(rt.Socket.TakeOverAllPorts, IDR_TRAY_MENU, ID_NETWORK_TROUBLESHOOTREMOTEADDRESSES_TAKEOVERALLPORTS);
	menu(rt.FramerateControl.UseMoreCpuTime, IDR_TRAY_MENU, ID_CONFIGURE_USEMORECPUTIME);
	menu(rt.FramerateControl.SynchronizeProcessing, IDR_TRAY_MENU, ID_CONFIGURE_SYNCHRONIZEPROCESSING);
	menu(rt.FramerateControl.UseBackgroundLimit, IDR_TRAY_MENU, ID_CONFIGURE_BACKGROUND_FRAMERATE_LIMIT);
	menu(rt.Opcodes.CheckForUpdatesOnStartup, IDR_TRAY_MENU, ID_CONFIGURE_CHECKFORUPDATEDOPCODESONSTARTUP);
	menu(rt.Opcodes.UseOpcodeFinder, IDR_TRAY_MENU, ID_NETWORK_USEIPCTYPEFINDER);
	menu(rt.Opcodes.UseAllIpcMessageLogger, IDR_TRAY_MENU, ID_NETWORK_USEALLIPCMESSAGELOGGER);
	menu(rt.Modding.Enabled, IDR_TRAY_MENU, ID_MODDING_ENABLE);
	m_itemInfo[&rt.Modding.Enabled].DescriptionId = IDS_SETTINGS_MODDING_ENABLE_DESC;
	menu(rt.Modding.Logging.AllDataFileRead, IDR_TRAY_MENU, ID_MODDING_LOGALLFILEACCESS);
	menu(rt.Modding.MuteVoice.Battle, IDR_TRAY_MENU, ID_MODDING_MUTEVOICE_BATTLE);
	menu(rt.Modding.MuteVoice.Cm, IDR_TRAY_MENU, ID_MODDING_MUTEVOICE_CM);
	menu(rt.Modding.MuteVoice.Emote, IDR_TRAY_MENU, ID_MODDING_MUTEVOICE_EMOTE);
	menu(rt.Modding.MuteVoice.Line, IDR_TRAY_MENU, ID_MODDING_MUTEVOICE_LINE);
	menu(rt.Modding.UseAltCodecMusicSupport, IDR_TRAY_MENU, ID_MODDING_USEALTCODECMUSICSUPPORT);
	label(rt.Audio.OutputSamplingRate, IDS_MENU_MODDING_SAMPLINGRATE);
	label(rt.Audio.SoxrResampler.Enabled, IDS_MENU_USESOXRRESAMPLER);

	label(rt.FramerateControl.BackgroundLimit, IDS_SETTINGS_BACKGROUNDLIMIT);
	label(rt.FramerateControl.Lock.Interval, IDS_SETTINGS_LOCK_INTERVAL);
	label(rt.FramerateControl.Lock.Automatic, IDS_SETTINGS_LOCK_AUTOMATIC);
	label(rt.FramerateControl.Lock.TargetFramerateRangeFrom, IDS_SETTINGS_LOCK_RANGEFROM);
	label(rt.FramerateControl.Lock.TargetFramerateRangeTo, IDS_SETTINGS_LOCK_RANGETO);
	label(rt.FramerateControl.Lock.MaximumRenderIntervalDeviation, IDS_SETTINGS_LOCK_MAXDEVIATION);
	label(rt.FramerateControl.Lock.GlobalCooldown, IDS_SETTINGS_LOCK_GCD);
	label(rt.Modding.Languages.LipSyncLanguage, IDS_SETTINGS_LIPSYNC, IDS_SETTINGS_LIPSYNC_DESC);

	auto& fixes = rt.CrowdFix.Fixes;
	label(rt.CrowdFix.Enabled, IDS_SETTINGS_CROWDFIX_ENABLED, IDS_SETTINGS_CROWDFIX_ENABLED_DESC);
	label(fixes.SkipIdleNotifiers, IDS_SETTINGS_CROWDFIX_SKIPIDLENOTIFIERS, IDS_SETTINGS_CROWDFIX_SKIPIDLENOTIFIERS_DESC);
	label(fixes.ChainWorkerWakeups, IDS_SETTINGS_CROWDFIX_CHAINWORKERWAKEUPS, IDS_SETTINGS_CROWDFIX_CHAINWORKERWAKEUPS_DESC);
	label(fixes.DedupeSkeletonSyncs, IDS_SETTINGS_CROWDFIX_DEDUPESKELETONSYNCS, IDS_SETTINGS_CROWDFIX_DEDUPESKELETONSYNCS_DESC);
	label(fixes.TrimCullingClear, IDS_SETTINGS_CROWDFIX_TRIMCULLINGCLEAR, IDS_SETTINGS_CROWDFIX_TRIMCULLINGCLEAR_DESC);
	label(fixes.ShortenAllocatorLock, IDS_SETTINGS_CROWDFIX_SHORTENALLOCATORLOCK, IDS_SETTINGS_CROWDFIX_SHORTENALLOCATORLOCK_DESC);
	label(fixes.PoolStagingBlocks, IDS_SETTINGS_CROWDFIX_POOLSTAGINGBLOCKS, IDS_SETTINGS_CROWDFIX_POOLSTAGINGBLOCKS_DESC);
	label(fixes.FreezeHiddenMinions, IDS_SETTINGS_CROWDFIX_FREEZEHIDDENMINIONS, IDS_SETTINGS_CROWDFIX_FREEZEHIDDENMINIONS_DESC);
	label(fixes.SkipPrepareWait, IDS_SETTINGS_CROWDFIX_SKIPPREPAREWAIT, IDS_SETTINGS_CROWDFIX_SKIPPREPAREWAIT_DESC);
	label(fixes.InlineBgPrep, IDS_SETTINGS_CROWDFIX_INLINEBGPREP, IDS_SETTINGS_CROWDFIX_INLINEBGPREP_DESC);
	label(fixes.SkipHiddenHotbars, IDS_SETTINGS_CROWDFIX_SKIPHIDDENHOTBARS, IDS_SETTINGS_CROWDFIX_SKIPHIDDENHOTBARS_DESC);
	label(fixes.ParallelAnimTail, IDS_SETTINGS_CROWDFIX_PARALLELANIMTAIL, IDS_SETTINGS_CROWDFIX_PARALLELANIMTAIL_DESC);
	label(fixes.SplitCharacterCulling, IDS_SETTINGS_CROWDFIX_SPLITCHARACTERCULLING, IDS_SETTINGS_CROWDFIX_SPLITCHARACTERCULLING_DESC);
	label(fixes.PerItemCullingClaims, IDS_SETTINGS_CROWDFIX_PERITEMCULLINGCLAIMS, IDS_SETTINGS_CROWDFIX_PERITEMCULLINGCLAIMS_DESC);
	label(fixes.GatherUsedCommands, IDS_SETTINGS_CROWDFIX_GATHERUSEDCOMMANDS, IDS_SETTINGS_CROWDFIX_GATHERUSEDCOMMANDS_DESC);

	for (const ConfigItemBase* group : {
		static_cast<ConfigItemBase*>(&rt.Ui.MainWindow),
		static_cast<ConfigItemBase*>(&rt.Ui.LogWindow),
		static_cast<ConfigItemBase*>(&rt.Ui.ConfigWindow),
		static_cast<ConfigItemBase*>(&rt.Modding.MuteVoice),
		static_cast<ConfigItemBase*>(&rt.Modding.Ttmp),
		static_cast<ConfigItemBase*>(&rt.Audio.SoxrResampler),
		static_cast<ConfigItemBase*>(&rt.Audio.SoxrResampler.Filter),
		static_cast<ConfigItemBase*>(&rt.CrowdFix.Fixes),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.Faces),
	})
		m_itemInfo[group].Inline = true;

	m_itemInfo[&rt.Opcodes].Actions = {{0, ID_CONFIGURE_EDITOPCODECONFIGURATION}};
	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.Modding.Ttmp.SearchDirectories),
		static_cast<ConfigItemBase*>(&rt.Modding.Ttmp.ChoicesFiles),
		static_cast<ConfigItemBase*>(&rt.Modding.PathReplacements),
		static_cast<ConfigItemBase*>(&rt.Modding.GameResourceFileEntryRootDirectories),
		static_cast<ConfigItemBase*>(&rt.Modding.AdditionalSqpackRootDirectories),
	})
		m_itemInfo[item].Tabbed = true;
	m_itemInfo[&rt.Ui.MainWindow].HeadingItem = &rt.Ui.MainWindow.Show;
	m_itemInfo[&rt.Audio.SoxrResampler].HeadingItem = &rt.Audio.SoxrResampler.Enabled;
	m_itemInfo[&rt.Ui.LogWindow].HeadingItem = &rt.Ui.LogWindow.Show;
	for (const ConfigItemBase* group : {
		static_cast<ConfigItemBase*>(&rt.Ui.MainWindow),
		static_cast<ConfigItemBase*>(&rt.Ui.LogWindow),
		static_cast<ConfigItemBase*>(&rt.Ui.ConfigWindow),
		static_cast<ConfigItemBase*>(&rt.Modding.Logging),
	})
		m_itemInfo[group].Flow = true;
	label(rt.Launch.UseLoginSessionSwitching, IDS_SETTINGS_LAUNCH_SESSIONSWITCHING);
	label(rt.Launch.ClearCopiedCommandLineSeconds, IDS_SETTINGS_LAUNCH_CLEARCOPIED, IDS_SETTINGS_LAUNCH_CLEARCOPIED_DESC);
	m_itemInfo[&rt.Opcodes].AppendNode = &m_config->Game;
	m_itemInfo[&rt.Opcodes].AppendHeadingId = IDS_SETTINGS_HEADING_OPCODECONFIG;

	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.ChainLoad.D3d11),
		static_cast<ConfigItemBase*>(&rt.ChainLoad.Dxgi),
		static_cast<ConfigItemBase*>(&rt.ChainLoad.Dinput8),
	})
		m_itemInfo[item].PickFiles = true;

	// Worked out by the repository, set by the version prompt, or shown on a page of its own; the animation lock
	// durations are left to editing the file.
	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.Launch.RememberedLanguage),
		static_cast<ConfigItemBase*>(&rt.Launch.RememberedRegion),
		static_cast<ConfigItemBase*>(&rt.Ui.MainWindow.SettingsTreeWidth),
		static_cast<ConfigItemBase*>(&rt.Opcodes.CheckForUpdatesOnStartup),
		static_cast<ConfigItemBase*>(&rt.NetworkTiming.ExpectedAnimationLockDurationUs),
		static_cast<ConfigItemBase*>(&rt.NetworkTiming.MaximumAnimationLockDurationUs),
		static_cast<ConfigItemBase*>(&rt.FramerateControl.UseMainThreadTimingHandler),
		static_cast<ConfigItemBase*>(&rt.Opcodes.VersionSensitiveFeaturesAllowedGameVersion),
		static_cast<ConfigItemBase*>(&rt.Opcodes.EnabledPatchCodes),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.Faces.Families),
	})
		m_itemInfo[item].Hidden = true;

	// What turning version-sensitive features off turns off.
	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.Launch.UseLoginSessionSwitching),
		static_cast<ConfigItemBase*>(&rt.Modding.Enabled),
		static_cast<ConfigItemBase*>(&rt.Opcodes.EnabledPatchCodes),
		static_cast<ConfigItemBase*>(&rt.Modding.UseAltCodecMusicSupport),
		static_cast<ConfigItemBase*>(&rt.GameWindow.UseImeModeIndicator),
		static_cast<ConfigItemBase*>(&rt.CrowdFix.Enabled),
		static_cast<ConfigItemBase*>(&rt.Audio.OutputSamplingRate),
		static_cast<ConfigItemBase*>(&rt.Audio.SoxrResampler.Enabled),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.Enabled),
	})
		m_itemInfo[item].VersionSensitive = true;

	m_fontFamilies = Apps::MainApp::FontReplacement::GameFontNames::Families();
	if (m_fontFamilies.empty())
		m_fontFamilies = {"AXIS", "Jupiter", "JupiterN", "Meidinger", "MiedingerMid", "TrumpGothic"};
	for (const auto& family : rt.FontReplacement.Faces.Families.Value() | std::views::keys) {
		if (std::ranges::find(m_fontFamilies, family) == m_fontFamilies.end())
			m_fontFamilies.push_back(family);
	}
	std::vector<TreeNode> fontFamilyNodes;
	for (const auto& family : m_fontFamilies)
		fontFamilyNodes.push_back({.Kind = TreeNode::NodeKind::FontFamily, .Family = family});

	// Positions of the top-level menus in the menu resource: 1 Restart, 2 Network, 4 Configure, 6 Help.
	using Kind = TreeNode::NodeKind;
	const auto commands = [this](std::vector<UINT> commandIds) {
		std::vector<std::pair<std::wstring, std::function<void()>>> buttons;
		for (const auto commandId : commandIds)
			buttons.emplace_back(GetMenuText(IDR_TRAY_MENU, commandId), [this, commandId] { PostMessageW(m_hWndOwner, WM_COMMAND, commandId, 0); });
		CreateActionRow(std::move(buttons));
	};
	m_tree = {
		{.Kind = Kind::Status, .LabelId = IDS_SETTINGS_PAGE_STATUS},
		{.Kind = Kind::Restart, .MenuPosition = 1, .Groups = {&rt.Launch}},
		// The network, the modding, and the framerate first; the rest by their English names, in every language.
		{.LabelId = IDS_SETTINGS_PAGE_GAMEBEHAVIOR, .Children = {
			{.MenuPosition = 2, .Groups = {&rt.NetworkTiming, &rt.Socket}, .Headings = true, .Children = {
				{.Groups = {&rt.Opcodes}},
			}, .AddFirst = [commands] { commands({ID_NETWORK_RELEASEALLCONNECTIONS, ID_NETWORK_RESETALLCONNECTIONS}); }},
			{.Groups = {&rt.Modding}, .Children = {
				{.Kind = Kind::TtmpFolder},
				{.Groups = {&rt.FontReplacement}, .Children = std::move(fontFamilyNodes)},
				{.Groups = {&rt.Modding.Languages}},
				{.Groups = {&rt.Modding.Logging}},
			}, .AddFirst = [commands] { commands({ID_MODDING_TTMP_REFRESH}); }},
			{.Groups = {&rt.FramerateControl}, .Children = {
				{.Kind = Kind::FramerateLocking, .Groups = {&rt.FramerateControl.Lock}},
			}},
			{.Groups = {&rt.Audio}},
			{.Groups = {&rt.CrowdFix}},
			{.Kind = Kind::PatchCodes, .LabelId = IDS_SETTINGS_PAGE_PATCHCODES},
			{.Groups = {&rt.GameWindow}},
		}},
		// The loading of other DLLs on a page of its own.
		{.LabelId = IDS_SETTINGS_PAGE_CONFIGURATION, .Groups = {&rt.Ui}, .Headings = true, .Children = {
			{.Groups = {&rt.ChainLoad}},
		}, .AddLast = [this, &rt, commands] {
			CreateHeadingRow(rt.GetStringRes(IDS_SETTINGS_UPDATES));
			commands({ID_FILE_CHECKFORUPDATES, ID_CONFIGURE_CHECKFORUPDATEDOPCODES});
			CreateRow(rt.Opcodes.CheckForUpdatesOnStartup);
			CreateHeadingRow(GetTopLevelMenuText(4));
			commands({ID_CONFIGURE_EDITRUNTIMECONFIGURATION, ID_CONFIGURE_OPENCONFIGURATIONDIRECTORY, ID_CONFIGURE_RELOAD});
			CreateHeadingRow(GetTopLevelMenuText(6));
			commands({ID_HELP_OPENHELPWEBPAGE, ID_HELP_OPENHOMEPAGE});
		}},
	};
	ExpandTree(m_tree);
	AttachTtmpNodes(true);
}

const XivAlexander::Apps::MainApp::Window::SettingsView::ItemInfo* XivAlexander::Apps::MainApp::Window::SettingsView::FindItemInfo(const ConfigItemBase& item) const {
	const auto it = m_itemInfo.find(&item);
	return it == m_itemInfo.end() ? nullptr : &it->second;
}

std::wstring XivAlexander::Apps::MainApp::Window::SettingsView::GetMenuText(UINT menuId, UINT commandId) const {
	const auto menu = Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCEW(menuId), m_config->Runtime.GetLangId());
	wchar_t buf[256]{};
	if (!GetMenuStringW(menu, commandId, buf, static_cast<int>(std::size(buf)), MF_BYCOMMAND))
		return {};
	return CleanMenuText(buf);
}

std::wstring XivAlexander::Apps::MainApp::Window::SettingsView::GetItemLabel(const ConfigItemBase& item) const {
	std::wstring label;
	if (const auto info = FindItemInfo(item)) {
		if (info->LabelId)
			label = CleanMenuText(m_config->Runtime.GetStringRes(info->LabelId));
		else if (info->MenuId)
			label = GetMenuText(info->MenuId, info->MenuCommand);
	}
	if (label.empty())
		label = xivres::util::unicode::convert<std::wstring>(item.Name);
	return label;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ApplyLanguage() {
	PostMessageW(m_hWnd, WmPopulateTree, 0, 0);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ApplyTheme() {
	PostMessageW(m_hWnd, WmRefreshTheme, 0, 0);
}

std::wstring XivAlexander::Apps::MainApp::Window::SettingsView::GetTopLevelMenuText(int position) const {
	const auto menu = Utils::Win32::Menu(Dll::Module(), RT_MENU, MAKEINTRESOURCEW(IDR_TRAY_MENU), m_config->Runtime.GetLangId());
	wchar_t buf[256]{};
	if (!GetMenuStringW(menu, position, buf, static_cast<int>(std::size(buf)), MF_BYPOSITION))
		return {};
	return CleanMenuText(buf);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ExpandTree(std::vector<TreeNode>& nodes) const {
	for (auto& node : nodes) {
		ExpandTree(node.Children);
		if (node.Kind != TreeNode::NodeKind::Groups || node.Groups.size() != 1)
			continue;

		std::set<const ConfigGroup*> given;
		for (const auto& child : node.Children)
			given.insert(child.Groups.begin(), child.Groups.end());

		std::vector<TreeNode> subgroups;
		for (const auto item : node.Groups.front()->Items()) {
			const auto subgroup = dynamic_cast<const ConfigGroup*>(item);
			if (!subgroup || given.contains(subgroup))
				continue;
			if (const auto info = FindItemInfo(*subgroup); info && info->Inline)
				continue;
			subgroups.push_back({.Groups = {subgroup}});
		}
		ExpandTree(subgroups);
		node.Children.insert(node.Children.begin(), std::make_move_iterator(subgroups.begin()), std::make_move_iterator(subgroups.end()));
	}
}

std::wstring XivAlexander::Apps::MainApp::Window::SettingsView::GetNodeLabel(const TreeNode& node) const {
	if (node.Kind == TreeNode::NodeKind::FontFamily)
		return xivres::util::unicode::convert<std::wstring>(node.Family);
	if (node.Kind == TreeNode::NodeKind::TtmpFolder && node.TtmpPath.empty())
		return GetItemLabel(m_config->Runtime.Modding.Ttmp);
	if (node.Kind == TreeNode::NodeKind::TtmpFolder || node.Kind == TreeNode::NodeKind::TtmpPack)
		return node.TtmpPath.filename().wstring();
	if (node.LabelId)
		return CleanMenuText(m_config->Runtime.GetStringRes(node.LabelId));
	if (node.MenuPosition >= 0)
		return GetTopLevelMenuText(node.MenuPosition);
	if (!node.Groups.empty())
		return GetItemLabel(*node.Groups.front());
	return {};
}

void XivAlexander::Apps::MainApp::Window::SettingsView::PopulateTree() {
	// An item's lParam is its node.
	const auto selected = reinterpret_cast<LPARAM>(m_pNode);
	HTREEITEM hSelected{};

	m_populatingTree = true;
	TreeView_DeleteAllItems(m_hTree);
	const std::function<void(const TreeNode&, HTREEITEM)> add = [&](const TreeNode& node, HTREEITEM hParent) {
		auto label = GetNodeLabel(node);
		TVINSERTSTRUCTW tvis{
			.hParent = hParent,
			.hInsertAfter = TVI_LAST,
			.item = {
				.mask = TVIF_TEXT | TVIF_PARAM,
				.pszText = label.data(),
				.lParam = reinterpret_cast<LPARAM>(&node),
			},
		};
		const auto hItem = TreeView_InsertItem(m_hTree, &tvis);
		if (reinterpret_cast<LPARAM>(&node) == selected)
			hSelected = hItem;
		for (const auto& child : node.Children)
			add(child, hItem);
		if (node.Kind != TreeNode::NodeKind::TtmpFolder)
			TreeView_Expand(m_hTree, hItem, TVE_EXPAND);
	};
	for (const auto& node : m_tree)
		add(node, TVI_ROOT);
	m_populatingTree = false;

	// Selecting shows the page, labelled anew.
	TreeView_SelectItem(m_hTree, hSelected ? hSelected : TreeView_GetRoot(m_hTree));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ClearRows() {
	m_rowCleanup.clear();
	m_fontPreview.reset();
	m_deferredTabbed.clear();
	TruncateRows(0);
	m_ttmpPage.reset();
	m_scrollY = 0;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::TruncateRows(size_t count) {
	for (auto i = count; i < m_rows.size(); ++i) {
		const auto& row = m_rows[i];
		for (const auto hWnd : {row->Label, row->Control, row->Button1, row->Button2, row->Button3, row->Button4, row->Description, row->Secondary}) {
			if (hWnd)
				DestroyWindow(hWnd);
		}
		for (const auto hWnd : row->Tabs)
			DestroyWindow(hWnd);
	}
	if (count < m_rows.size())
		m_rows.erase(m_rows.begin() + static_cast<ptrdiff_t>(count), m_rows.end());
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ShowPage(const TreeNode* pNode) {
	ClearRows();
	m_pNode = pNode;

	// A node without a page of its own shows its first child's; it stays selected, so that the arrow keys go past it.
	auto pShown = pNode;
	while (pShown && pShown->Kind == TreeNode::NodeKind::Groups && pShown->Groups.empty() && !pShown->Children.empty())
		pShown = &pShown->Children.front();

	const auto status = pShown && pShown->Kind == TreeNode::NodeKind::Status;
	ShowWindow(m_hPage, status ? SW_HIDE : SW_SHOW);
	ShowWindow(m_hStatus, status ? SW_SHOW : SW_HIDE);
	if (status) {
		InvalidateRect(m_hStatus, nullptr, FALSE);
		return;
	}

	if (pShown) {
		switch (pShown->Kind) {
			case TreeNode::NodeKind::PatchCodes:
				AddPatchCodeRows();
				break;

			case TreeNode::NodeKind::FramerateLocking:
				AddFramerateLockingRows();
				break;

			case TreeNode::NodeKind::FontFamily:
				AddFontFamilyRows(pShown->Family);
				break;

			case TreeNode::NodeKind::TtmpFolder:
				AddTtmpFolderRows(pShown->TtmpPath);
				break;

			case TreeNode::NodeKind::TtmpPack:
				AddTtmpPackRows(pShown->TtmpPath);
				break;

			case TreeNode::NodeKind::Restart:
				// The launch group's items are placed among the restart's.
				AddRestartRows();
				break;

			default:
				if (pShown->AddFirst)
					pShown->AddFirst();
				for (const auto group : pShown->Groups)
					AddGroupSection(*group, pShown->Headings);
				if (pShown->AddLast)
					pShown->AddLast();
				AddTabbedRows();
				break;
		}
	}
	RefreshRows();
	LayoutPage();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddGroupSection(const ConfigGroup& group, bool heading) {
	const auto info = FindItemInfo(group);
	if (heading && info && info->HeadingItem) {
		CreateRow(*const_cast<ConfigItemBase*>(info->HeadingItem));
		if (!m_rows.empty() && m_rows.back()->Item == info->HeadingItem && m_rows.back()->Type == Row::RowType::Check) {
			m_rows.back()->Bold = true;
			SendMessageW(m_rows.back()->Control, WM_SETFONT, reinterpret_cast<WPARAM>(*m_boldFont), FALSE);
		}
	} else if (heading && std::ranges::any_of(group.Items(), [this](const ConfigItemBase* item) {
		const auto info = FindItemInfo(*item);
		return !dynamic_cast<const ConfigGroup*>(item) && !(info && (info->Hidden || info->Tabbed));
	})) {
		CreateHeadingRow(GetItemLabel(group));
	}

	if (info) {
		for (size_t i = 0; i < info->Actions.size(); i += 2) {
			const auto action = [this](const std::pair<UINT, UINT>& labelAndCommand) {
				const auto [labelId, commandId] = labelAndCommand;
				return std::pair{
					labelId ? std::wstring(m_config->Runtime.GetStringRes(labelId)) : GetMenuText(IDR_TRAY_MENU, commandId),
					std::function<void()>([this, commandId] { PostMessageW(m_hWndOwner, WM_COMMAND, commandId, 0); }),
				};
			};
			auto [label1, onClick1] = action(info->Actions[i]);
			if (i + 1 < info->Actions.size()) {
				auto [label2, onClick2] = action(info->Actions[i + 1]);
				CreateActionRow(std::move(label1), std::move(onClick1), std::move(label2), std::move(onClick2));
			} else {
				CreateActionRow(std::move(label1), std::move(onClick1));
			}
		}
	}

	AddGroupRows(group);

	if (info && info->AppendNode) {
		CreateHeadingRow(m_config->Runtime.GetStringRes(info->AppendHeadingId));
		AddGroupRows(*info->AppendNode);
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddGroupRows(const ConfigNode& node) {
	// A group's check boxes may go side by side: each group's of its own.
	const auto nodeGroup = dynamic_cast<const ConfigGroup*>(&node);
	const auto nodeInfo = nodeGroup ? FindItemInfo(*nodeGroup) : nullptr;
	const auto flowGroup = nodeInfo && nodeInfo->Flow ? static_cast<int>(reinterpret_cast<uintptr_t>(&node) & 0x7FFFFFFF) : 0;
	for (const auto item : node.Items()) {
		if (dynamic_cast<const ConfigGroup*>(item))
			continue;
		if (const auto info = FindItemInfo(*item); info && info->Hidden)
			continue;
		if (nodeInfo && nodeInfo->HeadingItem == item)
			continue;
		if (const auto info = FindItemInfo(*item); info && info->Tabbed) {
			m_deferredTabbed.push_back(item);
			continue;
		}
		CreateRow(*item);
		if (flowGroup && !m_rows.empty() && m_rows.back()->Item == item && m_rows.back()->Type == Row::RowType::Check && !m_rows.back()->Description)
			m_rows.back()->FlowGroup = flowGroup;
	}

	// Inline groups follow, each under its heading, with theirs in turn.
	for (const auto item : node.Items()) {
		const auto group = dynamic_cast<const ConfigGroup*>(item);
		if (const auto info = group ? FindItemInfo(*group) : nullptr; info && info->Inline)
			AddGroupSection(*group, true);
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddPatchCodeRows() {
	auto& rt = m_config->Runtime;
	auto& enabled = rt.Opcodes.EnabledPatchCodes;
	const auto entries = m_config->PatchCode.GetEntries();

	// As the menu has them. A fix is enabled by its digest, of its contents: renaming its file keeps it enabled.
	const auto mark = rt.AreVersionSensitiveFeaturesDisabledTemporarily() ? L"(!) " : L"";
	auto& grid = CreateGridRow({}, 0, 220, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Width = 240},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_FILENAME), .Width = 200},
	}, {
		.GetRowCount = [entries] { return entries->size(); },
		.GetText = [entries, mark](size_t row, size_t column) {
			const auto& entry = (*entries)[row];
			return column == 1 ? std::format(L"{}{}", mark, Wide(entry.Patch.Name)) : entry.Path.filename().wstring();
		},
		.GetEditText = [entries](size_t row, size_t column) {
			const auto& entry = (*entries)[row];
			return column == 1 ? Wide(entry.Patch.Name) : entry.Path.filename().wstring();
		},
		.GetChecked = [entries, &enabled](size_t row, size_t) { return std::ranges::find(enabled.Value(), (*entries)[row].Digest) != enabled.Value().end(); },
		.SetText = [this, entries](size_t row, size_t column, const std::wstring& text) {
			const auto& entry = (*entries)[row];
			return column == 1 ? RenamePatchCodeName(entry.Path, entry.Digest, text) : RenamePatchCode(entry.Path, text);
		},
		.SetChecked = [entries, &enabled](size_t row, size_t, bool checked) {
			auto digests = enabled.Value();
			const auto& digest = (*entries)[row].Digest;
			const auto it = std::ranges::find(digests, digest);
			if (checked && it == digests.end())
				digests.push_back(digest);
			else if (!checked && it != digests.end())
				digests.erase(it);
			enabled = std::move(digests);
		},
		.Activate = [this, entries](size_t row) {
			const auto& entry = (*entries)[row];
			OpenPatchCodeEditor(entry.Path.filename().wstring(), entry.Path);
		},
		.ShowContextMenu = [this, entries](size_t row, POINT ptScreen) {
			const auto& entry = (*entries)[row];
			enum : UINT { IdEdit = 1, IdDelete, IdShow };
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
			AppendMenuW(hMenu, MF_STRING, IdEdit, m_config->Runtime.GetStringRes(IDS_SETTINGS_PATCHCODE_EDIT));
			AppendMenuW(hMenu, MF_STRING, IdDelete, m_config->Runtime.GetStringRes(IDS_SETTINGS_PATCHCODE_DELETE));
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(hMenu, MF_STRING, IdShow, m_config->Runtime.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER));
			SetMenuDefaultItem(hMenu, IdEdit, FALSE);
			switch (TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr)) {
				case IdEdit:
					OpenPatchCodeEditor(entry.Path.filename().wstring(), entry.Path);
					break;
				case IdDelete:
					DeletePatchCode(entry.Digest, entry.Path);
					break;
				case IdShow:
					OpenInExplorer(m_hWndOwner, entry.Path);
					break;
			}
		},
	});
	const auto selected = [&grid, entries]() -> const PatchCodeRepository::Entry* {
		const auto row = grid.GetSelectedRow();
		return row && *row < entries->size() ? &(*entries)[*row] : nullptr;
	};
	CreateActionRow({
		{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_NEW), [this] { CreatePatchCode(); }},
		{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_EDIT), [this, selected] {
			if (const auto entry = selected())
				OpenPatchCodeEditor(entry->Path.filename().wstring(), entry->Path);
		}},
		{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_DELETE), [this, selected] {
			if (const auto entry = selected())
				DeletePatchCode(entry->Digest, entry->Path);
		}},
		{GetMenuText(IDR_TRAY_MENU, ID_CONFIGURE_GAMEFIX_OPENDIRECTORY), [this, selected] {
			// With the selected one's file selected.
			if (const auto entry = selected())
				OpenInExplorer(m_hWndOwner, entry->Path);
			else
				PostMessageW(m_hWndOwner, WM_COMMAND, ID_CONFIGURE_GAMEFIX_OPENDIRECTORY, 0);
		}},
	});
	if (entries->empty())
		CreateNoteRow(GetMenuText(IDR_TRAY_MENU, ID_CONFIGURE_GAMEFIX_EMPTY));

	// The repository is reloaded from another thread when the files change.
	m_rowCleanup += m_config->PatchCode.OnChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });
	m_rowCleanup += enabled.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

bool XivAlexander::Apps::MainApp::Window::SettingsView::RenamePatchCode(const std::filesystem::path& path, const std::wstring& text) {
	auto name = std::wstring(xivres::util::trim(text));
	if (name.empty() || name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
		return false;
	if (_wcsicmp(std::filesystem::path(name).extension().c_str(), L".json") != 0)
		name += L".json";

	const auto target = path.parent_path() / name;
	if (target == path)
		return true;
	std::error_code ec;
	if (exists(target, ec) && _wcsicmp(target.c_str(), path.c_str()) != 0)
		return false;
	std::filesystem::rename(path, target, ec);
	if (ec) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, ec.message());
		return false;
	}
	m_config->PatchCode.Reload();
	return true;
}

bool XivAlexander::Apps::MainApp::Window::SettingsView::RenamePatchCodeName(const std::filesystem::path& path, const std::string& digest, const std::wstring& text) {
	const auto name = Utf8(Trim(text));
	if (name.empty())
		return false;

	try {
		auto json = Utils::ParseJsonFromFile(path);
		if (json.value("Name", std::string()) == name)
			return true;
		json["Name"] = name;

		// The key too, in case loading it would have made one.
		const auto patch = json.get<PatchInstruction>();
		json["HmacKey"] = patch.HmacKey;
		Utils::SaveJsonToFile(path, json);

		// A fix is enabled by its digest, which its name is a part of.
		auto& enabled = m_config->Runtime.Opcodes.EnabledPatchCodes;
		auto digests = enabled.Value();
		if (const auto it = std::ranges::find(digests, digest); it != digests.end()) {
			*it = patch.Digest();
			enabled = std::move(digests);
		}
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		return false;
	}
	m_config->PatchCode.Reload();
	return true;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::DeletePatchCode(const std::string& digest, const std::filesystem::path& path) {
	if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.FormatStringRes(IDS_SETTINGS_PATCHCODE_CONFIRMDELETE, path.filename().wstring())) != IDYES)
		return;

	auto& enabled = m_config->Runtime.Opcodes.EnabledPatchCodes;
	auto digests = enabled.Value();
	if (const auto it = std::ranges::find(digests, digest); it != digests.end()) {
		digests.erase(it);
		enabled = std::move(digests);
	}

	std::error_code ec;
	if (!remove(path, ec) && ec)
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, ec.message());
	m_config->PatchCode.Reload();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddRestartRows() {
	auto& rt = m_config->Runtime;
	const auto title = [&rt](UINT stringId) {
		// The first line of a task dialog button's text.
		std::wstring text = rt.GetStringRes(stringId);
		return CleanMenuText(text.substr(0, text.find(L'\n')));
	};

	// As the menu does: asks whether to restart or to launch another.
	CreateActionRow(
		std::format(L"{} / {}...", title(IDS_RESTART_GAME_RESTART), title(IDS_RESTART_GAME_NEWINSTANCE)),
		[this] { PostMessageW(m_hWndOwner, WM_COMMAND, ID_RESTART_RESTART, 0); },
		GetMenuText(IDR_TRAY_MENU, ID_RESTART_COPYLAUNCHCOMMANDLINE),
		[this] { PostMessageW(m_hWndOwner, WM_COMMAND, ID_RESTART_COPYLAUNCHCOMMANDLINE, 0); });

	// What the next restart uses, as the menu had them, each kind on lines of its own; choosing one doesn't restart, as
	// the menu did.
	const auto choice = [this](UINT commandId, bool checkBox, int flowGroup) {
		CreateChoiceRow(GetMenuText(IDR_TRAY_MENU, commandId), m_restartChoices.GetState(commandId).second,
			[this, commandId] { return m_restartChoices.GetState(commandId).first; },
			[this, commandId] { m_restartChoices.Choose(commandId); },
			checkBox, flowGroup);
	};
	for (const auto commandId : {ID_RESTART_USEXIVALEXANDER, ID_RESTART_USEPARAMETEROBFUSCATION, ID_RESTART_USEELEVATION})
		choice(commandId, true, 1);

	// The languages and regions that can't be chosen, of other publishers or for an installation that doesn't allow it,
	// aren't shown; nor is remembering them when none is.
	const auto choices = [this, &rt, &choice](const std::wstring& heading, std::initializer_list<UINT> commandIds, UINT rememberId, int flowGroup) {
		if (std::ranges::none_of(commandIds, [this](UINT commandId) { return m_restartChoices.GetState(commandId).second; }))
			return;
		CreateHeadingRow(heading, rt.GetStringRes(IDS_SETTINGS_REMEMBER), m_restartChoices.GetState(rememberId).second,
			[this, rememberId] { return m_restartChoices.GetState(rememberId).first; },
			[this, rememberId] { m_restartChoices.Choose(rememberId); });
		for (const auto commandId : commandIds) {
			if (m_restartChoices.GetState(commandId).second)
				choice(commandId, false, flowGroup);
		}
	};
	choices(rt.GetStringRes(IDS_SETTINGS_GRID_LANGUAGE), {
		ID_RESTART_LANGUAGE_JAPANESE, ID_RESTART_LANGUAGE_ENGLISH, ID_RESTART_LANGUAGE_GERMAN, ID_RESTART_LANGUAGE_FRENCH,
		ID_RESTART_LANGUAGE_SIMPLIFIEDCHINESE, ID_RESTART_LANGUAGE_KOREAN, ID_RESTART_LANGUAGE_CHINESETRADITIONAL,
	}, ID_RESTART_LANGUAGE_REMEMBER, 2);
	choices(rt.GetStringRes(IDS_SETTINGS_RESTART_REGION), {ID_RESTART_REGION_JAPAN, ID_RESTART_REGION_NORTH_AMERICA, ID_RESTART_REGION_EUROPE}, ID_RESTART_REGION_REMEMBER, 4);

	// Switching the sessions, and the sessions to choose from.
	// Forgetting the stored ones is by the heading, asked first; the launch arguments' session isn't stored.
	auto anyStored = false;
	if (auto& loginSessions = m_app.GetLoginSessions()) {
		m_rowCleanup += loginSessions->OnChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });
		anyStored = std::ranges::any_of(loginSessions->GetSessions(), [](const auto& session) { return !session.Alias.empty(); });
	}
	CreateHeadingRow(std::format(L"{}{}", rt.AreVersionSensitiveFeaturesDisabledTemporarily() ? L"(!) " : L"", CleanMenuText(rt.GetStringRes(IDS_MENU_LOGINSESSION))),
		rt.GetStringRes(IDS_SETTINGS_LAUNCH_SESSIONSWITCHING), true,
		[&rt] { return rt.Launch.UseLoginSessionSwitching.Value(); },
		[&rt] { rt.Launch.UseLoginSessionSwitching.Toggle(); },
		anyStored ? std::wstring(rt.GetStringRes(IDS_SETTINGS_SESSIONS_FORGET)) : std::wstring(),
		anyStored ? std::function<void()>([this] {
			if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.GetStringRes(IDS_SETTINGS_SESSIONS_FORGET_CONFIRM)) != IDYES)
				return;
			if (auto& sessions = m_app.GetLoginSessions())
				sessions->ForgetStoredSessions();
		}) : std::function<void()>());
	AddSessionRows();

	CreateHeadingRow(rt.GetStringRes(IDS_SETTINGS_MISCELLANEOUS));
	CreateRow(rt.Launch.ClearCopiedCommandLineSeconds);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddSessionRows() {
	auto& rt = m_config->Runtime;
	m_rowCleanup += rt.Launch.UseLoginSessionSwitching.OnChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });

	// As the menu has them: with two or more, by name, the unnamed launch session first.
	auto& loginSessions = m_app.GetLoginSessions();
	if (!rt.Launch.UseLoginSessionSwitching || !loginSessions)
		return;
	m_rowCleanup += loginSessions->OnChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });

	const auto sessions = loginSessions->GetSessions();
	if (sessions.size() < 2)
		return;

	std::vector<std::pair<std::wstring, size_t>> order;
	for (size_t k = 0; k < sessions.size(); k++)
		order.emplace_back(xivres::util::unicode::convert<std::wstring>(sessions[k].Alias), k);
	std::ranges::sort(order, [](const auto& l, const auto& r) {
		return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, l.first.c_str(), -1, r.first.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
	});

	for (const auto& [alias, k] : order) {
		auto label = alias.empty() ? std::wstring(rt.GetStringRes(IDS_MENU_LOGINSESSION_LAUNCHARGUMENTS)) : alias;
		if (sessions[k].Expired)
			label = rt.FormatStringRes(IDS_MENU_LOGINSESSION_EXPIRED, label);
		CreateChoiceRow(label, !sessions[k].Expired,
			[this, index = k] {
				const auto& loginSessions = m_app.GetLoginSessions();
				return loginSessions && loginSessions->GetSelectedIndex() == index;
			},
			[this, index = k] {
				if (auto& loginSessions = m_app.GetLoginSessions())
					loginSessions->Select(index);
			},
			false, 1);
		if (alias.empty())
			continue;
		m_rows.back()->ContextMenu = [this, alias = xivres::util::unicode::convert<std::string>(alias), label](POINT ptScreen) {
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
			AppendMenuW(hMenu, MF_STRING, 1, m_config->Runtime.GetStringRes(IDS_SETTINGS_SESSIONS_FORGETONE));
			if (TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr) != 1)
				return;
			if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.FormatStringRes(IDS_SETTINGS_SESSIONS_FORGETONE_CONFIRM, label)) != IDYES)
				return;
			if (auto& loginSessions = m_app.GetLoginSessions())
				loginSessions->ForgetStoredSession(alias);
		};
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddFramerateLockingRows() {
	auto& rt = m_config->Runtime;
	auto& lock = rt.FramerateControl.Lock;

	// Disabled, a fixed framerate, or a framerate chosen from the cooldown as it is measured.
	const auto getMode = [&lock] { return lock.Automatic ? 2 : lock.Interval ? 1 : 0; };
	const auto mode = getMode();

	// The global cooldown the game last reported, or UINT64_MAX.
	const auto measuredCooldownUs = [this] {
		if (const auto& handler = m_app.GetNetworkTimingHandler())
			return handler->GetCooldownGroup(Features::NetworkTimingHandler::CooldownGroup::Id_Gcd).DurationUs;
		return UINT64_MAX;
	};
	const auto bestIntervalUs = [&lock](uint64_t cooldownUs) {
		return RuntimeConfigRepository::CalculateLockFramerateIntervalUs(lock.TargetFramerateRangeFrom, lock.TargetFramerateRangeTo, cooldownUs, lock.MaximumRenderIntervalDeviation);
	};
	const auto estimate = [&lock](uint64_t cooldownUs, uint64_t intervalUs) -> std::wstring {
		if (!intervalUs || !cooldownUs || cooldownUs == UINT64_MAX)
			return L"-";
		const auto [shortest, longest] = RuntimeConfigRepository::EstimateLockedCooldownUs(cooldownUs, intervalUs, lock.MaximumRenderIntervalDeviation);
		return std::format(L"{:.4f} ~ {:.4f}", shortest / 1000000., longest / 1000000.);
	};

	// The dialog has the estimates of how many cooldowns fit a duration, and back.
	CreateActionRow(rt.GetStringRes(IDS_SETTINGS_OPENFRAMERATELOCKING), [this] { PostMessageW(m_hWndOwner, WM_COMMAND, ID_CONFIGURE_LOCKFRAMERATE, 0); });

	CreateComboRow(rt.GetStringRes(IDS_SETTINGS_LOCK_MODE),
		{rt.GetStringRes(IDS_SETTINGS_LOCK_MODE_OFF), rt.GetStringRes(IDS_SETTINGS_LOCK_MODE_FIXED), rt.GetStringRes(IDS_SETTINGS_LOCK_MODE_AUTOMATIC)},
		getMode,
		[&lock, bestIntervalUs](int newMode) {
			const auto batch = lock.Batch();
			lock.Automatic = newMode == 2;
			if (newMode == 0)
				lock.Interval = 0;
			else if (newMode == 1 && !lock.Interval)
				lock.Interval = bestIntervalUs(lock.GlobalCooldown * 10000);
		});

	if (mode == 1) {
		CreateNumberRow(rt.GetStringRes(IDS_SETTINGS_LOCK_FRAMERATE),
			[&lock] { return lock.Interval ? std::format(L"{:g}", 1000000. / lock.Interval) : std::wstring(L"0"); },
			[&lock](const std::wstring& text) {
				if (const auto fps = ParseNumber<double>(text); fps && *fps >= 1)
					lock.Interval = std::clamp<uint64_t>(static_cast<uint64_t>(1000000. / *fps), 1, 1000000);
			});
		CreateRow(lock.Interval);
		CreateNumberRow(rt.GetStringRes(IDS_SETTINGS_LOCK_GCD),
			[&lock] { return std::format(L"{}.{:02}", lock.GlobalCooldown / 100, lock.GlobalCooldown % 100); },
			[&lock](const std::wstring& text) {
				if (const auto seconds = ParseNumber<double>(text); seconds && *seconds > 0)
					lock.GlobalCooldown = static_cast<uint64_t>(std::round(*seconds * 100));
			});
		CreateValueRow(rt.GetStringRes(IDS_SETTINGS_LOCK_ESTIMATEDGCD), [&lock, estimate] { return estimate(lock.GlobalCooldown * 10000, lock.Interval); });
	} else if (mode == 2) {
		CreateValueRow(rt.GetStringRes(IDS_SETTINGS_LOCK_MEASUREDGCD), [measuredCooldownUs] {
			const auto cooldownUs = measuredCooldownUs();
			return cooldownUs == UINT64_MAX ? std::wstring(L"-") : std::format(L"{:.2f}", cooldownUs / 1000000.);
		});
		CreateValueRow(rt.GetStringRes(IDS_SETTINGS_LOCK_INTERVALINUSE), [measuredCooldownUs, bestIntervalUs] {
			const auto cooldownUs = measuredCooldownUs();
			return cooldownUs == UINT64_MAX ? std::wstring(L"-") : std::format(L"{}", bestIntervalUs(cooldownUs));
		});
		CreateValueRow(rt.GetStringRes(IDS_SETTINGS_LOCK_ESTIMATEDGCD), [measuredCooldownUs, bestIntervalUs, estimate] {
			const auto cooldownUs = measuredCooldownUs();
			return cooldownUs == UINT64_MAX ? std::wstring(L"-") : estimate(cooldownUs, bestIntervalUs(cooldownUs));
		});
	}

	// What the interval is chosen within: automatically, or with the button for a fixed one.
	if (mode != 0) {
		CreateRow(lock.TargetFramerateRangeFrom);
		CreateRow(lock.TargetFramerateRangeTo);
		CreateRow(lock.MaximumRenderIntervalDeviation);
	}
	if (mode == 1) {
		CreateActionRow(rt.GetStringRes(IDS_SETTINGS_LOCK_MINIMIZECLIPPING), [&lock, bestIntervalUs] {
			lock.Interval = bestIntervalUs(lock.GlobalCooldown * 10000);
		});
	}

	CreateListRow(rt.GetStringRes(IDS_SETTINGS_LOCK_LATESTGCDS), rt.GetStringRes(IDS_SETTINGS_LOCK_RESET),
		[this] {
			// As the dialog lists them.
			std::vector<std::wstring> items;
			for (const auto& [durationUs, driftUs] : m_cooldownHistory)
				items.emplace_back(std::format(L"{}.{:02}s {:+07}us", durationUs / 1000000, durationUs % 1000000 / 10000, driftUs));
			return items;
		},
		[this] {
			m_cooldownHistory.clear();
			RefreshRows();
		});

	// A change of mode changes what is shown; others, the values.
	m_rowCleanup += lock.OnChange([this, mode, getMode] { PostMessageW(m_hWnd, getMode() != mode ? WmRebuildPage : WmRefreshRows, 0, 0); });
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateValueRow(const std::wstring& label, std::function<std::wstring()> getText) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Value;
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);
	row.Control = CreateRowControl(PartControl, 0, WC_STATICW, L"", SS_LEFT | SS_NOPREFIX);
	row.Refresh = [&row, getText = std::move(getText)] { SetWindowTextW(row.Control, getText().c_str()); };
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateNumberRow(const std::wstring& label, std::function<std::wstring()> getText, std::function<void(const std::wstring&)> setText) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Edit;
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);
	row.Control = CreateRowControl(PartControl, WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL);
	row.Refresh = [&row, getText = std::move(getText)] { SetWindowTextW(row.Control, getText().c_str()); };
	row.Commit = [&row, setText = std::move(setText)] {
		// What can't be read goes back to what was there.
		setText(GetText(row.Control));
		row.Refresh();
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateComboRow(const std::wstring& label, const std::vector<std::wstring>& choices, std::function<int()> getChoice, std::function<void(int)> setChoice) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Combo;
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);
	row.Control = CreateRowControl(PartControl, 0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
	for (const auto& choice : choices)
		ComboBox_AddString(row.Control, choice.c_str());
	row.Refresh = [&row, getChoice = std::move(getChoice)] { ComboBox_SetCurSel(row.Control, getChoice()); };
	row.Commit = [&row, setChoice = std::move(setChoice)] {
		if (const auto selected = ComboBox_GetCurSel(row.Control); selected >= 0)
			setChoice(selected);
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateListRow(const std::wstring& label, const std::wstring& buttonLabel, std::function<std::vector<std::wstring>()> getItems, std::function<void()> onButton) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::PathList;  // Laid out as one.
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);
	row.Control = CreateRowControl(PartControl, WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT);
	row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, buttonLabel, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY);
	row.Refresh = [&row, getItems = std::move(getItems)] {
		const auto top = ListBox_GetTopIndex(row.Control);
		SendMessageW(row.Control, WM_SETREDRAW, FALSE, 0);
		ListBox_ResetContent(row.Control);
		for (const auto& item : getItems())
			ListBox_AddString(row.Control, item.c_str());
		ListBox_SetTopIndex(row.Control, top);
		SendMessageW(row.Control, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(row.Control, nullptr, TRUE);
	};
	row.Click = [onButton = std::move(onButton)](int part) {
		if (part == PartButton1)
			onButton();
	};
	m_rows.emplace_back(std::move(pRow));
}

XivAlexander::Apps::MainApp::Window::GridView& XivAlexander::Apps::MainApp::Window::SettingsView::CreateGridRow(const std::wstring& label, UINT descriptionId, int height, std::vector<GridView::Column> columns, GridView::Source source) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Grid;
	row.Height = height;
	if (!label.empty())
		row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);
	if (descriptionId)
		row.Description = CreateRowControl(PartDescription, 0, WC_STATICW, m_config->Runtime.GetStringRes(descriptionId), SS_LEFT | SS_NOPREFIX);
	row.Grid = std::make_unique<GridView>(m_hPage, FirstRowControlId + static_cast<int>(m_rows.size()) * ControlsPerRow + PartControl, std::move(columns), std::move(source));
	row.Control = row.Grid->Handle();
	row.Grid->SetZoom(GetZoom(), *m_font);
	row.Grid->ApplyTheme(IsDarkModeEnabled());
	row.Refresh = [&row] { row.Grid->Refresh(); };
	auto& grid = *row.Grid;
	m_rows.emplace_back(std::move(pRow));
	return grid;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateSliderRow(const std::wstring& label, double minimum, double maximum, double step, std::function<double()> getValue, std::function<void(double)> setValue) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Slider;
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);
	row.Control = CreateRowControl(PartControl, 0, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS);
	row.Secondary = CreateRowControl(PartSecondary, WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL);
	const auto steps = static_cast<int>(std::round((maximum - minimum) / step));
	SendMessageW(row.Control, TBM_SETRANGE, FALSE, MAKELPARAM(0, steps));
	SendMessageW(row.Control, TBM_SETPAGESIZE, 0, std::max(1, steps / 10));

	const auto clamp = [minimum, maximum](double value) { return std::clamp(value, minimum, maximum); };
	row.Refresh = [&row, getValue, minimum, step] {
		const auto value = getValue();
		SendMessageW(row.Control, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::round((value - minimum) / step)));
		SetWindowTextW(row.Secondary, std::format(L"{:g}", value).c_str());
	};
	row.Commit = [&row, setValue, clamp] {
		if (const auto value = ParseNumber<double>(GetText(row.Secondary)))
			setValue(clamp(*value));
		row.Refresh();
	};
	row.Click = [&row, setValue, minimum, step](int part) {
		// The slider moved.
		if (part == PartControl)
			setValue(minimum + step * static_cast<double>(SendMessageW(row.Control, TBM_GETPOS, 0, 0)));
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddPathReplacementRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.PathReplacements;
	auto& grid = CreateGridRow(GetItemLabel(item), IDS_SETTINGS_PATHREPLACEMENT_DESC, 200, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHREPLACEMENT_FROM), .Width = 220},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHREPLACEMENT_TO), .Width = 180},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHREPLACEMENT_STOP), .Kind = GridView::CellKind::Check, .Width = 60},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&item](size_t row, size_t column) { const auto& rule = item.Value()[row]; return Wide(column == 1 ? rule.From : rule.To); },
		.GetChecked = [&item](size_t row, size_t column) { const auto& rule = item.Value()[row]; return column == 0 ? rule.Enabled : rule.Stop; },
		.SetText = [&item](size_t row, size_t column, const std::wstring& text) {
			const auto value = Utf8(text);
			if (column == 1 && !IsValidRegex(value))
				return false;
			EditAt(item, row, [&](PathReplacementRule& rule) { (column == 1 ? rule.From : rule.To) = value; });
			return true;
		},
		.SetChecked = [&item](size_t row, size_t column, bool checked) {
			EditAt(item, row, [&](PathReplacementRule& rule) { (column == 0 ? rule.Enabled : rule.Stop) = checked; });
		},
		.MoveRow = MoveIn(item),
	});
	CreateActionRow({
		{rt.GetStringRes(IDS_SETTINGS_GRID_ADD), AddTo(item, grid, PathReplacementRule{}, 1)},
		{rt.GetStringRes(IDS_SETTINGS_REMOVE), RemoveFrom(item, grid)},
	});
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddPathFilterRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Logging.PathFilters;
	auto& grid = CreateGridRow(GetItemLabel(item), IDS_SETTINGS_PATHFILTER_DESC, 200, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHFILTER_PATTERN), .Width = 320},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHFILTER_INCLUDE), .Kind = GridView::CellKind::Check, .Width = 60},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&item](size_t row, size_t) { return Wide(item.Value()[row].Pattern); },
		.GetChecked = [&item](size_t row, size_t column) { const auto& filter = item.Value()[row]; return column == 0 ? filter.Enabled : filter.Include; },
		.SetText = [&item](size_t row, size_t, const std::wstring& text) {
			const auto value = Utf8(text);
			if (!IsValidRegex(value))
				return false;
			EditAt(item, row, [&](LogPathFilter& filter) { filter.Pattern = value; });
			return true;
		},
		.SetChecked = [&item](size_t row, size_t column, bool checked) {
			EditAt(item, row, [&](LogPathFilter& filter) { (column == 0 ? filter.Enabled : filter.Include) = checked; });
		},
		.MoveRow = MoveIn(item),
	});
	CreateActionRow({
		{rt.GetStringRes(IDS_SETTINGS_GRID_ADD), AddTo(item, grid, LogPathFilter{}, 1)},
		{rt.GetStringRes(IDS_SETTINGS_REMOVE), RemoveFrom(item, grid)},
	});
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddDirectoryListRows(ConfigItem<std::vector<std::filesystem::path>>& item, std::vector<std::filesystem::path> defaults, UINT descriptionId) {
	auto& rt = m_config->Runtime;

	// An empty list shows a row of its own that lists the defaults again, if there are any.
	const auto pDefaults = std::make_shared<const std::vector<std::filesystem::path>>(std::move(defaults));
	const auto isPlaceholder = [&item, pDefaults] { return item.Value().empty() && !pDefaults->empty(); };
	const auto resolve = [this, &item](size_t row) { return m_config->TranslateDirectoryPath(item.Value()[row]); };

	auto& grid = CreateGridRow(GetItemLabel(item), descriptionId, 150, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_FOLDER), .Width = 260},
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_LOCATION), .Kind = GridView::CellKind::ReadOnly, .Width = 320},
	}, {
		.GetRowCount = [&item, isPlaceholder] { return isPlaceholder() ? 1 : item.Value().size(); },
		.GetText = [&rt, &item, isPlaceholder, resolve](size_t row, size_t column) -> std::wstring {
			if (isPlaceholder())
				return column == 0 ? std::wstring(rt.GetStringRes(IDS_SETTINGS_DIRS_ADDDEFAULT)) : std::wstring();
			if (column == 0)
				return item.Value()[row].wstring();
			const auto path = resolve(row);
			std::error_code ec;
			return is_directory(path, ec) ? path.wstring() : rt.FormatStringRes(IDS_SETTINGS_DIRS_NOTFOUND, path.wstring());
		},
		.IsEditable = [isPlaceholder](size_t, size_t column) { return column == 0 && !isPlaceholder(); },
		.SetText = [&item](size_t row, size_t, const std::wstring& text) {
			auto value = Trim(text);
			if (value.empty())
				return false;
			EditAt(item, row, [&](std::filesystem::path& dir) { dir = std::move(value); });
			return true;
		},
		.MoveRow = MoveIn(item),
		.Activate = [this, &item, pDefaults, isPlaceholder, resolve](size_t row) {
			if (isPlaceholder())
				item = *pDefaults;
			else if (row < item.Value().size())
				OpenInExplorer(m_hWndOwner, resolve(row));
		},
	});
	CreateActionRow({
		{rt.GetStringRes(IDS_SETTINGS_DIRS_ADD), [this, &item, &grid] {
			if (const auto path = PickPath(m_hWndOwner, {}, PickKind::Folder, std::format("DirectoryList.{}", item.Name))) {
				auto values = item.Value();
				values.push_back(*path);
				item = std::move(values);
				grid.Refresh();
				grid.Select(item.Value().size() - 1);
			}
		}},
		{rt.GetStringRes(IDS_SETTINGS_REMOVE), [remove = RemoveFrom(item, grid), isPlaceholder] {
			if (!isPlaceholder())
				remove();
		}},
		{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [this, &item, &grid, isPlaceholder, resolve] {
			if (const auto row = grid.GetSelectedRow(); row && !isPlaceholder() && *row < item.Value().size())
				OpenInExplorer(m_hWndOwner, resolve(*row));
		}},
	});
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

struct XivAlexander::Apps::MainApp::Window::SettingsView::TtmpPage {
	std::shared_ptr<Features::Modding::NestedTtmp> Folder;
	std::vector<std::shared_ptr<Features::Modding::NestedTtmp>> Items;  // The folders and packs in the folder, in its order.
	std::vector<size_t> Shown;  // Of the items, those matching the filter.
	GridView* Grid{};
};

void XivAlexander::Apps::MainApp::Window::SettingsView::ListenToTtmps() {
	if (m_listeningToTtmps)
		return;
	if (auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks()) {
		// Told on whichever thread changed them, after every change, including an option chosen here.
		m_cleanup += sqpacks->OnTtmpSetsChanged([this] { PostMessageW(m_hWnd, WmTtmpSetsChanged, 0, 0); });
		m_listeningToTtmps = true;
	}
}

bool XivAlexander::Apps::MainApp::Window::SettingsView::AttachTtmpNodes(bool force) {
	// Under the node of the TexTools ModPacks, which is the page of those in no folder: the folders as they nest and the
	// packs, each folder with its own, all in the library's order.
	TreeNode* pGroupNode{};
	const std::function<void(std::vector<TreeNode>&)> findGroupNode = [&](std::vector<TreeNode>& nodes) {
		for (auto& node : nodes) {
			if (node.Kind == TreeNode::NodeKind::TtmpFolder && node.TtmpPath.empty())
				pGroupNode = &node;
			else
				findGroupNode(node.Children);
		}
	};
	findGroupNode(m_tree);
	if (!pGroupNode)
		return false;

	std::vector<std::filesystem::path> paths;
	std::set<std::filesystem::path> disabled;
	std::vector<TreeNode> nodes;
	if (const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks()) {
		const auto lock = sqpacks->LockTtmps();
		const std::function<void(const Features::Modding::NestedTtmp&, std::vector<TreeNode>&, std::vector<TreeNode>&)> addChildren =
			[&](const Features::Modding::NestedTtmp& parent, std::vector<TreeNode>& folders, std::vector<TreeNode>& packs) {
				for (const auto& child : *parent.Children) {
					paths.push_back(child->Path);
					if (!child->Enabled)
						disabled.insert(child->Path);
					if (child->IsGroup()) {
						TreeNode node{.Kind = TreeNode::NodeKind::TtmpFolder, .TtmpPath = child->Path};
						addChildren(*child, node.Children, node.Children);
						folders.push_back(std::move(node));
					} else if (child->Ttmp) {
						packs.push_back({.Kind = TreeNode::NodeKind::TtmpPack, .TtmpPath = child->Path});
					}
				}
			};
		addChildren(*sqpacks->GetTtmps(), nodes, nodes);
	}

	// Struck through in the tree.
	if (disabled != m_ttmpDisabled) {
		m_ttmpDisabled = std::move(disabled);
		if (m_hTree)
			InvalidateRect(m_hTree, nullptr, FALSE);
	}

	// The tree's items point at the nodes, so they are only replaced along with them.
	if (!force && paths == m_ttmpNodePaths)
		return false;
	m_ttmpNodePaths = std::move(paths);
	pGroupNode->Children = std::move(nodes);
	return true;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::SelectNode(const std::function<bool(const TreeNode&)>& predicate) {
	const std::function<HTREEITEM(HTREEITEM)> find = [&](HTREEITEM hItem) -> HTREEITEM {
		for (; hItem; hItem = TreeView_GetNextSibling(m_hTree, hItem)) {
			TVITEMW item{.mask = TVIF_PARAM, .hItem = hItem};
			if (TreeView_GetItem(m_hTree, &item) && predicate(*reinterpret_cast<const TreeNode*>(item.lParam)))
				return hItem;
			if (const auto hFound = find(TreeView_GetChild(m_hTree, hItem)))
				return hFound;
		}
		return nullptr;
	};
	if (const auto hItem = find(TreeView_GetRoot(m_hTree)))
		TreeView_SelectItem(m_hTree, hItem);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::OnTtmpSetsChanged() {
	ListenToTtmps();

	using Kind = TreeNode::NodeKind;
	const auto shownKind = m_pNode ? m_pNode->Kind : Kind::Groups;
	const auto shownPath = m_pNode ? m_pNode->TtmpPath : std::filesystem::path();
	const auto showingTtmp = shownKind == Kind::TtmpFolder || shownKind == Kind::TtmpPack;
	if (AttachTtmpNodes(false)) {
		// Where it went by a rename or a move here; else what was shown, if it still is there; else, for a ModPack,
		// its folder; else the packs in no folder.
		if (!m_ttmpToReselect.empty()) {
			const std::function<const TreeNode*(const std::vector<TreeNode>&)> find = [&](const std::vector<TreeNode>& nodes) -> const TreeNode* {
				for (const auto& node : nodes) {
					if ((node.Kind == Kind::TtmpFolder || node.Kind == Kind::TtmpPack) && !node.TtmpPath.empty() && node.TtmpPath == m_ttmpToReselect)
						return &node;
					if (const auto found = find(node.Children))
						return found;
				}
				return nullptr;
			};
			if (const auto found = find(m_tree)) {
				m_pNode = found;
				m_ttmpToReselect.clear();
				PopulateTree();
				if (std::exchange(m_ttmpRenameAfterReselect, false)) {
					SetFocus(m_hTree);
					TreeView_EditLabel(m_hTree, TreeView_GetSelection(m_hTree));
				}
				return;
			}
		}
		if (showingTtmp) {
			const TreeNode* pExact{};
			const TreeNode* pFolder{};
			const TreeNode* pNoFolder{};
			const std::function<void(const std::vector<TreeNode>&)> find = [&](const std::vector<TreeNode>& nodes) {
				for (const auto& node : nodes) {
					if (node.Kind == shownKind && node.TtmpPath == shownPath)
						pExact = &node;
					else if (node.Kind == Kind::TtmpFolder && !node.TtmpPath.empty() && node.TtmpPath == shownPath.parent_path())
						pFolder = &node;
					else if (node.Kind == Kind::TtmpFolder && node.TtmpPath.empty())
						pNoFolder = &node;
					find(node.Children);
				}
			};
			find(m_tree);
			m_pNode = pExact ? pExact : pFolder ? pFolder : pNoFolder;
		}
		PopulateTree();
		return;
	}

	if (shownKind == Kind::TtmpPack) {
		RefreshRows();
		return;
	}
	if (shownKind != Kind::TtmpFolder)
		return;

	// The same folders and packs, in the same order: only what they show; else the page anew, keeping the filter.
	std::vector<std::filesystem::path> packs;
	if (const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks(); sqpacks && m_ttmpPage && m_ttmpPage->Folder) {
		const auto lock = sqpacks->LockTtmps();
		for (const auto& child : *m_ttmpPage->Folder->Children)
			packs.push_back(child->Path);
	}
	if (packs == m_ttmpPagePacks && m_ttmpPage && m_ttmpPage->Folder)
		RefreshRows();
	else
		ShowPage(m_pNode);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTtmpFolderRows(const std::filesystem::path& folder) {
	using Features::Modding::NestedTtmp;
	auto& rt = m_config->Runtime;
	const auto menuText = [this](UINT commandId) { return GetMenuText(IDR_TRAY_MENU, commandId); };
	const auto command = [this](UINT commandId) { return [this, commandId] { PostMessageW(m_hWndOwner, WM_COMMAND, commandId, 0); }; };

	m_ttmpPagePacks.clear();
	AddTtmpProfileRow();
	const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks) {
		CreateActionRow({{menuText(ID_MODDING_TTMP_IMPORT), command(ID_MODDING_TTMP_IMPORT)}, {menuText(ID_MODDING_TTMP_REFRESH), command(ID_MODDING_TTMP_REFRESH)}});
		CreateNoteRow(menuText(ID_MODDING_TTMP_NOTREADY));
		return;
	}

	const auto page = std::make_shared<TtmpPage>();
	{
		const auto lock = sqpacks->LockTtmps();
		if (folder.empty()) {
			page->Folder = sqpacks->GetTtmps();
		} else {
			const std::function<void(const std::shared_ptr<NestedTtmp>&)> find = [&](const std::shared_ptr<NestedTtmp>& parent) {
				for (const auto& child : *parent->Children) {
					if (!child->IsGroup())
						continue;
					if (child->Path == folder)
						page->Folder = child;
					else
						find(child);
				}
			};
			find(sqpacks->GetTtmps());
		}
		if (page->Folder) {
			for (const auto& child : *page->Folder->Children) {
				if (child->Ttmp || child->IsGroup()) {
					page->Items.push_back(child);
					m_ttmpPagePacks.push_back(child->Path);
				}
			}
		}
	}
	if (!page->Folder) {
		CreateNoteRow(menuText(ID_MODDING_TTMP_NOENTRY));
		return;
	}
	m_ttmpPage = page;

	// Whether the folder is used, and what is done with it; the top level has neither a switch nor a name.
	const auto pFolder = page->Folder;
	if (folder.empty()) {
		CreateActionRow({{menuText(ID_MODDING_TTMP_IMPORT), command(ID_MODDING_TTMP_IMPORT)}, {menuText(ID_MODDING_TTMP_REFRESH), command(ID_MODDING_TTMP_REFRESH)}});
	} else {
		CreateEnabledRow(
			[pFolder] { return pFolder->Enabled; },
			[this, pFolder] { SetTtmpEnabled(*pFolder, !pFolder->Enabled); },
			{
				{rt.GetStringRes(IDS_SETTINGS_TTMP_RENAME), [this] { RenameShownTtmp(); }},
				{rt.GetStringRes(IDS_SETTINGS_TTMP_MOVETO), [this, pFolder] { ShowTtmpMoveMenu({pFolder}); }},
				{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [this, pFolder] { OpenInExplorer(m_hWndOwner, pFolder->Path); }},
				{{}, {}},
				{menuText(ID_MODDING_TTMP_IMPORT), command(ID_MODDING_TTMP_IMPORT)},
				{menuText(ID_MODDING_TTMP_REFRESH), command(ID_MODDING_TTMP_REFRESH)},
			});
	}

	if (page->Items.empty()) {
		CreateNoteRow(menuText(ID_MODDING_TTMP_NOENTRY));
		return;
	}

	// Applied as it is typed.
	CreateNumberRow(rt.GetStringRes(IDS_SETTINGS_TTMP_FILTER), {}, {});
	auto& filterRow = *m_rows.back();
	filterRow.Refresh = nullptr;
	filterRow.Commit = [this, &filterRow] {
		m_ttmpFilter = GetText(filterRow.Control);
		FilterTtmps();
	};
	// Setting the text tells of a change at once; it is applied only after.
	SetWindowTextW(filterRow.Control, m_ttmpFilter.c_str());
	filterRow.CommitOnChange = true;

	// Several may be selected, and acted on together; a double click opens one's own page.
	const auto requiresRestart = menuText(ID_MODDING_TTMP_ENTRY_REQUIRESRESTART);
	const std::wstring folderStatus = rt.GetStringRes(IDS_SETTINGS_DIRS_FOLDER);
	const auto packAt = [page](size_t row) -> NestedTtmp& { return *page->Items[page->Shown[row]]; };
	const auto selected = [page]() {
		std::vector<std::shared_ptr<NestedTtmp>> packs;
		if (page->Grid) {
			for (const auto row : page->Grid->GetSelectedRows()) {
				if (row < page->Shown.size())
					packs.push_back(page->Items[page->Shown[row]]);
			}
		}
		return packs;
	};
	auto& grid = CreateGridRow({}, IDS_SETTINGS_TTMP_ORDER_DESC, 260, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Kind = GridView::CellKind::ReadOnly, .Width = 260},
		{.Title = rt.GetStringRes(IDS_SETTINGS_TTMP_AUTHOR), .Kind = GridView::CellKind::ReadOnly, .Width = 140},
		{.Title = rt.GetStringRes(IDS_SETTINGS_TTMP_VERSION), .Kind = GridView::CellKind::ReadOnly, .Width = 70},
		{.Title = rt.GetStringRes(IDS_SETTINGS_TTMP_STATUS), .Kind = GridView::CellKind::ReadOnly, .Width = 120},
	}, {
		.GetRowCount = [page] { return page->Shown.size(); },
		.GetText = [packAt, requiresRestart, folderStatus](size_t row, size_t column) -> std::wstring {
			// A folder is named by its directory, and says it is one.
			const auto& item = packAt(row);
			if (!item.Ttmp)
				return column == 1 ? item.Path.filename().wstring() : column == 4 ? folderStatus : std::wstring();
			const auto& set = *item.Ttmp;
			switch (column) {
				case 1: return Wide(set.DisplayName());
				case 2: return Wide(set.List.Author);
				case 3: return Wide(set.List.Version);
				case 4: return set.Allocated ? std::wstring() : requiresRestart;
				default: return {};
			}
		},
		.GetChecked = [packAt](size_t row, size_t) { return packAt(row).Enabled; },
		.SetChecked = [this, packAt](size_t row, size_t, bool checked) { SetTtmpEnabled(packAt(row), checked); },
		.MoveRow = [this, page](size_t from, size_t to) {
			// Among all of them only: the order of some isn't where they go among the rest.
			if (!Trim(m_ttmpFilter).empty()) {
				MessageBeep(MB_ICONWARNING);
				return;
			}
			// Every child of the folder is listed, so the list in its new order is the folder's.
			auto children = page->Items;
			MoveWithin(children, from, to);
			auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
			RunTtmpOperation([&] { sqpacks->SetTtmpOrder(page->Folder, children); });
		},
		.Activate = [this, packAt](size_t row) {
			// After the grid is done with the click: the page it is on goes.
			m_ttmpPackToSelect = packAt(row).Path;
			PostMessageW(m_hWnd, WmSelectTtmpPack, 0, 0);
		},
		.ShowContextMenu = [this, selected](size_t, POINT ptScreen) {
			const auto packs = selected();
			if (packs.empty())
				return;
			enum : UINT { IdRename = 1, IdMove, IdNewFolder, IdDelete, IdShow };
			const auto single = packs.size() == 1 ? 0 : MF_GRAYED;
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
			AppendMenuW(hMenu, MF_STRING | single, IdRename, m_config->Runtime.GetStringRes(IDS_SETTINGS_TTMP_RENAME));
			AppendMenuW(hMenu, MF_STRING, IdMove, m_config->Runtime.GetStringRes(IDS_SETTINGS_TTMP_MOVETO));
			AppendMenuW(hMenu, MF_STRING, IdNewFolder, m_config->Runtime.GetStringRes(IDS_SETTINGS_TTMP_MOVETONEWFOLDER));
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			const auto onlyPacks = std::ranges::all_of(packs, [](const auto& item) { return item->Ttmp.has_value(); }) ? 0 : MF_GRAYED;
			AppendMenuW(hMenu, MF_STRING | onlyPacks, IdDelete, m_config->Runtime.GetStringRes(IDS_SETTINGS_PATCHCODE_DELETE));
			AppendMenuW(hMenu, MF_STRING | single, IdShow, m_config->Runtime.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER));
			switch (TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr)) {
				case IdRename:
					RenameTtmpInTree(packs.front()->Path);
					break;
				case IdMove:
					ShowTtmpMoveMenu(packs);
					break;
				case IdNewFolder:
					MoveTtmpToNewFolder(packs);
					break;
				case IdDelete:
					DeleteTtmps(packs);
					break;
				case IdShow:
					OpenInExplorer(m_hWndOwner, packs.front()->Path);
					break;
			}
		},
		.MultiSelect = true,
		.SetCheckedRows = [this, page](const std::vector<size_t>& rows, size_t, bool checked) {
			// Saved each, and applied to the game once.
			auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
			if (!sqpacks || rows.empty())
				return;
			try {
				std::vector<std::shared_ptr<NestedTtmp>> packs;
				{
					const auto lock = sqpacks->LockTtmps();
					for (const auto row : rows) {
						packs.push_back(page->Items[page->Shown[row]]);
						packs.back()->Enabled = checked;
					}
				}
				for (size_t i = 0; i < packs.size(); ++i)
					sqpacks->ApplyTtmpChanges(*packs[i], i + 1 == packs.size());
			} catch (const std::exception& e) {
				Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			}
		},
		.KeyDown = [this, selected](UINT vk) {
			if (vk == VK_DELETE) {
				// Packs only: a folder is deleted by deleting what is in it.
				if (const auto items = selected(); std::ranges::all_of(items, [](const auto& item) { return item->Ttmp.has_value(); }))
					DeleteTtmps(items);
				else
					MessageBeep(MB_ICONWARNING);
				return true;
			}
			if (vk == VK_F2) {
				if (const auto packs = selected(); packs.size() == 1)
					RenameTtmpInTree(packs.front()->Path);
				return true;
			}
			return false;
		},
	});
	page->Grid = &grid;

	FilterTtmps();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::FilterTtmps() {
	const auto page = m_ttmpPage;
	if (!page || !page->Grid)
		return;

	// By name or author, ignoring case.
	const auto lower = [](std::wstring s) {
		if (!s.empty())
			CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
		return s;
	};
	const auto needle = lower(Trim(m_ttmpFilter));
	page->Shown.clear();
	for (size_t i = 0; i < page->Items.size(); ++i) {
		const auto& item = *page->Items[i];
		const auto name = item.Ttmp ? Wide(item.Ttmp->DisplayName()) : item.Path.filename().wstring();
		const auto author = item.Ttmp ? Wide(item.Ttmp->List.Author) : std::wstring();
		if (needle.empty() || lower(name).find(needle) != std::wstring::npos || lower(author).find(needle) != std::wstring::npos)
			page->Shown.push_back(i);
	}
	page->Grid->ClearSelection();
	page->Grid->Refresh();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTtmpProfileRow() {
	// Which profile's choices are used, as the library picks it: the active one, or else the first. Only with more than one
	// to switch between.
	auto& item = m_config->Runtime.Modding.Ttmp.ChoicesFiles;
	const auto profiles = item.Value();
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });
	if (profiles.size() < 2)
		return;

	std::vector<std::wstring> names;
	for (const auto& profile : profiles)
		names.push_back(Wide(profile.Name.empty() ? profile.FileName : profile.Name));
	CreateComboRow(CleanMenuText(m_config->Runtime.GetStringRes(IDS_MENU_TTMP_PROFILE)), names,
		[&item] {
			const auto& current = item.Value();
			const auto it = std::ranges::find_if(current, [](const auto& profile) { return profile.Active && !profile.FileName.empty(); });
			return it == current.end() ? 0 : static_cast<int>(it - current.begin());
		},
		[&item](int index) {
			auto updated = item.Value();
			if (index < 0 || static_cast<size_t>(index) >= updated.size())
				return;
			for (size_t i = 0; i < updated.size(); ++i)
				updated[i].Active = static_cast<int>(i) == index;
			item = std::move(updated);
		});
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTtmpPackRows(const std::filesystem::path& path) {
	using Features::Modding::NestedTtmp;
	auto& rt = m_config->Runtime;
	AddTtmpProfileRow();
	const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks) {
		CreateNoteRow(GetMenuText(IDR_TRAY_MENU, ID_MODDING_TTMP_NOTREADY));
		return;
	}

	std::shared_ptr<NestedTtmp> pack;
	{
		const auto lock = sqpacks->LockTtmps();
		const std::function<void(const std::shared_ptr<NestedTtmp>&)> find = [&](const std::shared_ptr<NestedTtmp>& parent) {
			for (const auto& child : *parent->Children) {
				if (child->IsGroup())
					find(child);
				else if (child->Ttmp && child->Path == path)
					pack = child;
			}
		};
		find(sqpacks->GetTtmps());
	}
	if (!pack) {
		CreateNoteRow(GetMenuText(IDR_TRAY_MENU, ID_MODDING_TTMP_NOENTRY));
		return;
	}

	CreateEnabledRow(
		[pack] { return pack->Enabled; },
		[this, pack] { SetTtmpEnabled(*pack, !pack->Enabled); },
		{
			{rt.GetStringRes(IDS_SETTINGS_TTMP_RENAME), [this] { RenameShownTtmp(); }},
			{rt.GetStringRes(IDS_SETTINGS_TTMP_MOVETO), [this, pack] { ShowTtmpMoveMenu({pack}); }},
			{rt.GetStringRes(IDS_SETTINGS_TTMP_MOVETONEWFOLDER), [this, pack] { MoveTtmpToNewFolder({pack}); }},
			{{}, {}},
			{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_DELETE), [this, pack] { DeleteTtmps({pack}); }},
			{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [this, pack] { OpenInExplorer(m_hWndOwner, pack->Path); }},
		});
	AddTtmpDetailRows(pack);
}

std::shared_ptr<XivAlexander::Apps::MainApp::Features::Modding::NestedTtmp> XivAlexander::Apps::MainApp::Window::SettingsView::FindTtmp(const std::filesystem::path& path) const {
	using Features::Modding::NestedTtmp;
	const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks || path.empty())
		return nullptr;

	// A folder or a ModPack.
	const auto lock = sqpacks->LockTtmps();
	std::shared_ptr<NestedTtmp> found;
	const std::function<void(const std::shared_ptr<NestedTtmp>&)> find = [&](const std::shared_ptr<NestedTtmp>& parent) {
		for (const auto& child : *parent->Children) {
			if (child->Path == path)
				found = child;
			else if (child->IsGroup())
				find(child);
		}
	};
	find(sqpacks->GetTtmps());
	return found;
}

bool XivAlexander::Apps::MainApp::Window::SettingsView::RunTtmpOperation(const std::function<void()>& operation, std::filesystem::path reselect) {
	// The game is paused while the files move, which may take a few seconds if something holds them.
	const auto hPrevCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
	try {
		m_ttmpToReselect = std::move(reselect);
		operation();
		SetCursor(hPrevCursor);
		return true;
	} catch (const std::exception& e) {
		SetCursor(hPrevCursor);
		m_ttmpToReselect.clear();
		m_ttmpRenameAfterReselect = false;
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		return false;
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::RenameShownTtmp() {
	// In the tree, where the shown one is selected.
	if (!m_pNode || m_pNode->TtmpPath.empty() || (m_pNode->Kind != TreeNode::NodeKind::TtmpFolder && m_pNode->Kind != TreeNode::NodeKind::TtmpPack))
		return;
	SetFocus(m_hTree);
	TreeView_EditLabel(m_hTree, TreeView_GetSelection(m_hTree));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ShowTtmpMoveMenu(std::vector<std::shared_ptr<Features::Modding::NestedTtmp>> items) {
	using Features::Modding::NestedTtmp;
	auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks || items.empty())
		return;

	// The top level of each searched directory, then the folders as they nest; where they are is unavailable.
	std::vector<std::filesystem::path> targets;
	const auto hMenu = CreatePopupMenu();
	const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
	const auto parent = items.front()->Path.parent_path();
	const auto add = [&](const std::filesystem::path& dir, const std::wstring& label) {
		targets.push_back(dir);
		AppendMenuW(hMenu, MF_STRING | (items.size() == 1 && dir == parent ? MF_GRAYED : 0), targets.size(), label.c_str());
	};
	for (const auto& dir : sqpacks->GetTtmpSearchDirectories())
		add(dir, m_config->Runtime.FormatStringRes(IDS_SETTINGS_TTMP_TOPLEVEL, dir.wstring()));
	{
		const auto lock = sqpacks->LockTtmps();
		const std::function<void(const NestedTtmp&, size_t)> addFolders = [&](const NestedTtmp& folder, size_t depth) {
			for (const auto& child : *folder.Children) {
				if (!child->IsGroup() || std::ranges::find(items, child) != items.end())
					continue;
				add(child->Path, std::wstring(depth * 4, L' ') + child->Path.filename().wstring());
				addFolders(*child, depth + 1);
			}
		};
		if (!targets.empty())
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		addFolders(*sqpacks->GetTtmps(), 0);
	}

	POINT pt{};
	GetCursorPos(&pt);
	const auto chosen = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
	if (chosen <= 0 || static_cast<size_t>(chosen) > targets.size())
		return;
	const auto& target = targets[static_cast<size_t>(chosen) - 1];
	RunTtmpOperation([&] { sqpacks->MoveTtmps(items, target); }, items.size() == 1 ? target / items.front()->Path.filename() : std::filesystem::path());
}

void XivAlexander::Apps::MainApp::Window::SettingsView::MoveTtmpToNewFolder(const std::vector<std::shared_ptr<Features::Modding::NestedTtmp>>& items) {
	auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks || items.empty())
		return;

	// Next to the first, under a name not taken, to be named in the tree.
	const auto parent = items.front()->Path.parent_path();
	const std::wstring baseName = m_config->Runtime.GetStringRes(IDS_SETTINGS_TTMP_NEWFOLDER);
	auto name = baseName;
	std::error_code ec;
	for (auto i = 2; exists(parent / name, ec); ++i)
		name = std::format(L"{} ({})", baseName, i);

	std::filesystem::path created;
	m_ttmpRenameAfterReselect = true;
	RunTtmpOperation([&] { created = sqpacks->CreateTtmpFolder(parent, name, items); }, parent / name);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::RenameTtmpInTree(const std::filesystem::path& path) {
	// After the list is done with the key or the menu: its page goes once the pack's is shown.
	m_ttmpPackToSelect = path;
	m_ttmpRenameAfterSelect = true;
	PostMessageW(m_hWnd, WmSelectTtmpPack, 0, 0);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTtmpDetailRows(const std::shared_ptr<Features::Modding::NestedTtmp>& pack) {
	const auto& set = *pack->Ttmp;
	const auto& list = set.List;
	// Without what it doesn't say.
	auto heading = Wide(set.DisplayName());
	if (!list.Author.empty() && !list.Version.empty())
		heading += std::format(L" - {}, {}", Wide(list.Author), Wide(list.Version));
	else if (!list.Author.empty() || !list.Version.empty())
		heading += std::format(L" - {}", Wide(list.Author.empty() ? list.Version : list.Author));
	CreateHeadingRow(heading);
	if (!list.Description.empty())
		CreateRichTextRow(Wide(list.Description));
	if (!set.Allocated)
		CreateNoteRow(GetMenuText(IDR_TRAY_MENU, ID_MODDING_TTMP_ENTRY_REQUIRESRESTART));
	if (!list.Url.empty()) {
		CreateActionRow({{GetMenuText(IDR_TRAY_MENU, ID_HELP_OPENHOMEPAGE), [this, url = Wide(list.Url)] {
			try {
				Utils::Win32::ShellExecutePathOrThrow(url, m_hWndOwner);
			} catch (const std::exception& e) {
				Dll::MessageBoxF(m_hWndOwner, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
			}
		}}});
	}

	// Each group of options under its name: one of them, or any of them if it allows more.
	auto flowGroup = 0;
	for (size_t pageIndex = 0; pageIndex < list.ModPackPages.size(); ++pageIndex) {
		const auto& groups = list.ModPackPages[pageIndex].ModGroups;
		for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
			const auto& group = groups[groupIndex];
			if (group.OptionList.empty())
				continue;

			const auto multiple = group.SelectionType == "Multi";
			CreateHeadingRow(Wide(group.GroupName));
			++flowGroup;
			std::string descriptions;
			for (size_t optionIndex = 0; optionIndex < group.OptionList.size(); ++optionIndex) {
				const auto& option = group.OptionList[optionIndex];
				auto label = option.Name.empty() ? std::string("-") : option.Name;
				if (!option.GroupName.empty() && option.GroupName != group.GroupName)
					label += std::format(" ({})", option.GroupName);
				CreateChoiceRow(Wide(label), true,
					[pack, pageIndex, groupIndex, optionIndex] {
						try {
							for (const auto& chosen : pack->Ttmp->Choices.at(pageIndex).at(groupIndex)) {
								if (chosen.get<size_t>() == optionIndex)
									return true;
							}
						} catch (...) {
							// Not chosen, if the choices can't be read.
						}
						return false;
					},
					[this, pack, pageIndex, groupIndex, optionIndex, multiple] { ChooseTtmpOption(pack, pageIndex, groupIndex, optionIndex, multiple); },
					multiple, flowGroup);
				if (!option.Description.empty())
					descriptions += std::format("{}* {}: {}", descriptions.empty() ? "" : "\n", option.Name.empty() ? "-" : option.Name, option.Description);
			}
			if (!descriptions.empty())
				CreateNoteRow(Wide(descriptions));
		}
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::SetTtmpEnabled(Features::Modding::NestedTtmp& nestedTtmp, bool enabled) {
	auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks)
		return;
	try {
		{
			const auto lock = sqpacks->LockTtmps();
			nestedTtmp.Enabled = enabled;
		}
		sqpacks->ApplyTtmpChanges(nestedTtmp);
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ChooseTtmpOption(const std::shared_ptr<Features::Modding::NestedTtmp>& pack, size_t pageIndex, size_t groupIndex, size_t optionIndex, bool multiple) {
	auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks)
		return;
	try {
		{
			const auto lock = sqpacks->LockTtmps();
			auto& page = pack->Ttmp->Choices.at(pageIndex);
			if (multiple) {
				auto chosen = page.at(groupIndex).get<std::set<size_t>>();
				if (!chosen.erase(optionIndex))
					chosen.insert(optionIndex);
				page[groupIndex] = chosen;
			} else {
				page[groupIndex] = nlohmann::json::array({optionIndex});
			}
		}
		sqpacks->ApplyTtmpChanges(*pack);
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::DeleteTtmps(const std::vector<std::shared_ptr<Features::Modding::NestedTtmp>>& packs) {
	auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	std::vector<std::filesystem::path> lists;
	for (const auto& pack : packs) {
		if (pack->Ttmp)
			lists.push_back(pack->Ttmp->ListPath);
	}
	if (!sqpacks || lists.empty())
		return;

	const auto message = lists.size() == 1
		? m_config->Runtime.FormatStringRes(IDS_SETTINGS_TTMP_CONFIRMDELETE, Wide(packs.front()->Ttmp->DisplayName()), lists.front().wstring())
		: m_config->Runtime.FormatStringRes(IDS_SETTINGS_TTMP_CONFIRMDELETEMANY, lists.size());
	if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, message) != IDYES)
		return;
	try {
		for (size_t i = 0; i < lists.size(); ++i)
			sqpacks->DeleteTtmp(lists[i], i + 1 == lists.size());
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddFallbackPriorityRows() {
	// Every language, in the order used: those not listed come after the listed, so all are shown, and moving one lists them all.
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Languages.FallbackPriority;
	CreateGridRow(GetItemLabel(item), IDS_SETTINGS_FALLBACK_DESC, 190, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_LANGUAGE), .Kind = GridView::CellKind::ReadOnly, .Width = 240},
	}, {
		.GetRowCount = [&rt] { return rt.GetFallbackLanguageList().size(); },
		.GetText = [&rt](size_t row, size_t) { return rt.GetLanguageNameLocalized(rt.GetFallbackLanguageList()[row]); },
		.MoveRow = [&rt, &item](size_t from, size_t to) {
			auto languages = rt.GetFallbackLanguageList();
			MoveWithin(languages, from, to);
			item = std::move(languages);
		},
	});
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddForcedLanguageRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Languages.ForcedCharacterLanguages;
	auto& grid = CreateGridRow(GetItemLabel(item), IDS_SETTINGS_FORCEDLANGUAGE_DESC, 180, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Width = 220},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_LANGUAGE), .Kind = GridView::CellKind::ComboEdit, .Width = 100, .GetChoices = [](size_t) {
			// The voice files' suffixes.
			std::vector<std::wstring> codes;
			for (auto i = 1; i <= static_cast<int>(xivres::game_language::TraditionalChinese); ++i) {
				if (const auto code = Wide(xivres::game_language_code(static_cast<xivres::game_language>(i))); std::ranges::find(codes, code) == codes.end())
					codes.push_back(code);
			}
			return codes;
		}},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&rt, &item](size_t row, size_t column) {
			const auto& entry = item.Value()[row];
			if (column == 1)
				return entry.Name.empty() ? std::wstring(rt.GetStringRes(IDS_SETTINGS_FORCEDLANGUAGE_UNNAMED)) : Wide(entry.Name);
			return Wide(entry.Language);
		},
		.GetEditText = [&item](size_t row, size_t column) { const auto& entry = item.Value()[row]; return Wide(column == 1 ? entry.Name : entry.Language); },
		.GetChecked = [&item](size_t row, size_t) { return item.Value()[row].Enabled; },
		.SetText = [&item](size_t row, size_t column, const std::wstring& text) {
			EditAt(item, row, [&](ForcedCharacterLanguage& entry) { (column == 1 ? entry.Name : entry.Language) = Utf8(xivres::util::trim(text)); });
			return true;
		},
		.SetChecked = [&item](size_t row, size_t, bool checked) {
			EditAt(item, row, [&](ForcedCharacterLanguage& entry) { entry.Enabled = checked; });
		},
		.MoveRow = MoveIn(item),
	});
	CreateActionRow({
		{rt.GetStringRes(IDS_SETTINGS_GRID_ADD), AddTo(item, grid, ForcedCharacterLanguage{}, 1)},
		{rt.GetStringRes(IDS_SETTINGS_REMOVE), RemoveFrom(item, grid)},
	});
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddChoicesFileRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Ttmp.ChoicesFiles;
	auto& grid = CreateGridRow(GetItemLabel(item), IDS_SETTINGS_CHOICES_DESC, 160, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_CHOICES_ACTIVE), .Kind = GridView::CellKind::Check, .Width = 70},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Width = 180},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_FILENAME), .Width = 180},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&item](size_t row, size_t column) { const auto& profile = item.Value()[row]; return Wide(column == 1 ? profile.Name : profile.FileName); },
		.GetChecked = [&item](size_t row, size_t) { return item.Value()[row].Active; },
		.SetText = [&item](size_t row, size_t column, const std::wstring& text) {
			const auto value = Utf8(xivres::util::trim(text));
			if (column == 2 && value.find_first_of("\\/:*?\"<>|") != std::string::npos)
				return false;
			EditAt(item, row, [&](ChoicesProfile& profile) { (column == 1 ? profile.Name : profile.FileName) = value; });
			return true;
		},
		.SetChecked = [&item](size_t row, size_t, bool checked) {
			// None, or one.
			auto profiles = item.Value();
			for (size_t i = 0; i < profiles.size(); ++i)
				profiles[i].Active = i == row ? checked : false;
			item = std::move(profiles);
		},
		.MoveRow = MoveIn(item),
	});
	CreateActionRow({
		{rt.GetStringRes(IDS_SETTINGS_GRID_ADD), AddTo(item, grid, ChoicesProfile{.Active = false}, 1)},
		{rt.GetStringRes(IDS_SETTINGS_REMOVE), RemoveFrom(item, grid)},
	});
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddEdgeRows() {
	auto& rt = m_config->Runtime;
	auto& edge = rt.FontReplacement.Edge;
	const auto setter = [&edge](float FontReplacementEdgeConfig::* field) {
		return [&edge, field](double value) {
			auto config = edge.Value();
			config.*field = static_cast<float>(value);
			edge = config;
		};
	};
	// As FontReplacementEdgeConfig::Clamped has them.
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_EDGE_SCALE), 0, 1, 0.01, [&edge] { return static_cast<double>(edge.Value().Scale); }, setter(&FontReplacementEdgeConfig::Scale));
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_EDGE_MIN), 0.25, 8, 0.05, [&edge] { return static_cast<double>(edge.Value().Min); }, setter(&FontReplacementEdgeConfig::Min));
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_EDGE_MAX), 0.25, 8, 0.05, [&edge] { return static_cast<double>(edge.Value().Max); }, setter(&FontReplacementEdgeConfig::Max));
	m_rowCleanup += edge.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });

	// A preview to judge the edge by, of the family of most of the UI's text (or the first there is), drawn again as the
	// rows are refreshed: as the sliders move.
	auto family = std::string(EdgePreviewFamily);
	if (FontReplacement::GameFontNames::FacesOf(family).empty() && !m_fontFamilies.empty())
		family = m_fontFamilies.front();
	AddFontPreviewRows(family, true);
}

const std::filesystem::path& XivAlexander::Apps::MainApp::Window::SettingsView::GetFontPresetFolder() {
	if (m_fontPresetFolder.empty())
		m_fontPresetFolder = FontReplacement::Presets::Folder(*m_config);
	return m_fontPresetFolder;
}

std::vector<std::wstring> XivAlexander::Apps::MainApp::Window::SettingsView::ListPresets() {
	// Paths relative to the presets folder, as the sources keep them; not the font.json of glyph image folders imported
	// along with their presets, which are no presets.
	std::vector<std::wstring> res;
	const auto& folder = GetFontPresetFolder();
	std::error_code ec;
	if (!is_directory(folder, ec))
		return res;
	for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
		!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		if (it->is_regular_file(ec)
			&& _wcsicmp(it->path().extension().c_str(), L".json") == 0
			&& _wcsicmp(it->path().filename().c_str(), FontChanger::FixedSizeFont::glyph_files_metadata_file_name) != 0)
			res.emplace_back(it->path().lexically_relative(folder).wstring());
	}
	std::ranges::sort(res, [](const auto& l, const auto& r) { return _wcsicmp(l.c_str(), r.c_str()) < 0; });
	return res;
}

const std::vector<std::wstring>& XivAlexander::Apps::MainApp::Window::SettingsView::ListSystemFontFamilies() {
	// English names, as the sources keep them; read once.
	if (!m_systemFontFamilies.empty())
		return m_systemFontFamilies;
	IDWriteFactoryPtr factory;
	IDWriteFontCollectionPtr collection;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)))
		|| FAILED(factory->GetSystemFontCollection(&collection)))
		return m_systemFontFamilies;
	for (UINT32 i = 0, count = collection->GetFontFamilyCount(); i < count; ++i) {
		IDWriteFontFamilyPtr family;
		IDWriteLocalizedStringsPtr names;
		if (FAILED(collection->GetFontFamily(i, &family)) || FAILED(family->GetFamilyNames(&names)))
			continue;
		UINT32 index = 0;
		BOOL exists = FALSE;
		if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists)
			index = 0;
		UINT32 length = 0;
		if (FAILED(names->GetStringLength(index, &length)))
			continue;
		std::wstring name(static_cast<size_t>(length) + 1, L'\0');
		if (SUCCEEDED(names->GetString(index, name.data(), length + 1))) {
			name.resize(length);
			m_systemFontFamilies.push_back(std::move(name));
		}
	}
	std::ranges::sort(m_systemFontFamilies, [](const auto& l, const auto& r) { return _wcsicmp(l.c_str(), r.c_str()) < 0; });
	return m_systemFontFamilies;
}

std::optional<std::string> XivAlexander::Apps::MainApp::Window::SettingsView::ChooseFontPreset() {
	auto& rt = m_config->Runtime;
	const auto& folder = GetFontPresetFolder();
	const auto picked = PickPath(m_hWndOwner, folder, PickKind::Preset, "FontPreset");
	if (!picked)
		return std::nullopt;

	// One already in the presets folder is used by its path relative to it. Both are made canonical first, so that a
	// path through a link, or of another case, is still found to be in it.
	std::error_code ec;
	auto canonicalFolder = weakly_canonical(folder, ec);
	if (ec)
		canonicalFolder = absolute(folder).lexically_normal();
	auto canonicalPicked = weakly_canonical(*picked, ec);
	if (ec)
		canonicalPicked = absolute(*picked).lexically_normal();
	if (const auto relative = canonicalPicked.lexically_relative(canonicalFolder); !relative.empty() && *relative.begin() != L"..")
		return Utf8(relative.wstring());

	// Otherwise it is imported, or used where it is. Glyph images it reads from folders next to it would be left behind by
	// copying the preset alone; they are copied along with it if they are under its folder, and if any isn't, it can only
	// be used where it is.
	std::vector<std::filesystem::path> glyphFolders;
	try {
		glyphFolders = FontReplacement::Presets::RelativeGlyphImageFolders(*picked);
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_SETTINGS_FONT_PRESETUNREADABLE, picked->wstring(), Wide(e.what()));
		return std::nullopt;
	}
	const auto copyable = std::ranges::all_of(glyphFolders, &FontReplacement::Presets::IsCopyableGlyphImageFolder);

	std::wstring folderList;
	for (const auto& glyphFolder : glyphFolders)
		folderList += std::format(L"\n• {}", glyphFolder.wstring());
	auto content = rt.FormatStringRes(IDS_SETTINGS_FONT_IMPORT_CONTENT, picked->wstring());
	if (!glyphFolders.empty())
		content += L"\n\n" + rt.FormatStringRes(copyable ? IDS_SETTINGS_FONT_IMPORT_GLYPHS : IDS_SETTINGS_FONT_IMPORT_NOGLYPHCOPY, folderList);

	static constexpr int IdImport = 1001;
	static constexpr int IdInPlace = 1002;
	auto builder = Utils::Win32::TaskDialog::Builder();
	builder
		.WithWindowTitle(Dll::GetGenericMessageBoxTitle())
		.WithParentWindow(m_hWndOwner)
		.WithInstance(Dll::Module())
		.WithAllowDialogCancellation()
		.WithMainIcon(IDI_TRAY_ICON)
		.WithMainInstruction(std::wstring(rt.GetStringRes(IDS_SETTINGS_FONT_IMPORT_TITLE)))
		.WithContent(content);
	if (copyable)
		builder.WithButton({.IdSet = true, .Id = IdImport, .Text = std::wstring(rt.GetStringRes(IDS_SETTINGS_FONT_IMPORT_IMPORT))});
	builder
		.WithButton({.IdSet = true, .Id = IdInPlace, .Text = std::wstring(rt.GetStringRes(IDS_SETTINGS_FONT_IMPORT_INPLACE))})
		.WithCommonButton(TDCBF_CANCEL_BUTTON)
		.WithButtonCommandLinks()
		.WithButtonDefault(copyable ? IdImport : IdInPlace);

	int choice;
	try {
		choice = builder.Build().Show().Button;
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		return std::nullopt;
	}

	switch (choice) {
		case IdImport:
			try {
				return Utf8(FontReplacement::Presets::Import(*picked, folder, glyphFolders).wstring());
			} catch (const std::exception& e) {
				Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_SETTINGS_FONT_IMPORTFAILED, picked->wstring(), Wide(e.what()));
				return std::nullopt;
			}

		case IdInPlace:
			// As it was picked, not canonical: that may be through another drive's path (a mapped drive's share).
			return Utf8(picked->lexically_normal().wstring());

		default:
			// Closed, or cancelled.
			return std::nullopt;
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddFontFamilyRows(const std::string& family) {
	auto& rt = m_config->Runtime;
	auto& item = rt.FontReplacement.Faces.Families;

	// The family's settings, and setting them: a family as the defaults have it, on and without sources, is left out.
	const auto getFamily = [&item, family]() -> const FontReplacementFamily& {
		static const FontReplacementFamily defaults;
		const auto& all = item.Value();
		const auto it = all.find(family);
		return it == all.end() ? defaults : it->second;
	};
	const auto setFamily = [&item, family](FontReplacementFamily settings) {
		auto all = item.Value();
		if (settings == FontReplacementFamily{})
			all.erase(family);
		else
			all[family] = std::move(settings);
		item = std::move(all);
	};
	const auto get = [getFamily]() -> const std::vector<FontReplacementFamilySource>& { return getFamily().Sources; };
	const auto set = [getFamily, setFamily](std::vector<FontReplacementFamilySource> sources) {
		auto settings = getFamily();
		settings.Sources = std::move(sources);
		setFamily(std::move(settings));
	};
	const auto edit = [get, set](size_t row, auto&& fn) {
		auto sources = get();
		if (row >= sources.size())
			return;
		fn(sources[row]);
		set(std::move(sources));
	};
	const auto parseNumber = [](const std::wstring& text) { return std::wcstol(text.c_str(), nullptr, 10); };

	// The menu atop the page acts on the grid below it, which is made after it.
	const auto pGrid = std::make_shared<GridView*>();
	const auto add = [get, set, pGrid](FontReplacementFamilySource source, bool editName) {
		auto sources = get();
		sources.push_back(std::move(source));
		set(std::move(sources));
		auto& grid = **pGrid;
		grid.Refresh();
		if (editName)
			grid.BeginEdit(get().size() - 1, 2);
		else
			grid.Select(get().size() - 1);
	};
	const auto remove = [get, set, pGrid] {
		auto& grid = **pGrid;
		const auto selected = grid.GetSelectedRow();
		auto sources = get();
		if (!selected || *selected >= sources.size())
			return;
		sources.erase(sources.begin() + static_cast<ptrdiff_t>(*selected));
		set(std::move(sources));
		grid.Refresh();
		if (!get().empty())
			grid.Select(std::min(*selected, get().size() - 1));
	};

	// Whether the family is replaced, as a folder or a ModPack is used, with what is done with its sources in the menu.
	CreateEnabledRow(
		[getFamily] { return getFamily().Enabled; },
		[this, getFamily, setFamily] {
			auto settings = getFamily();
			settings.Enabled = !settings.Enabled;
			setFamily(std::move(settings));
			InvalidateRect(m_hTree, nullptr, FALSE);
		},
		{
			{rt.GetStringRes(IDS_SETTINGS_FONT_ADDPRESET), [this, add] {
				if (const auto preset = ChooseFontPreset())
					add({.Preset = *preset}, false);
			}},
			{rt.GetStringRes(IDS_SETTINGS_FONT_ADDFONT), [add] { add({.Font = {.Name = "Segoe UI"}}, true); }},
			{rt.GetStringRes(IDS_SETTINGS_REMOVE), remove},
			{{}, {}},
			{rt.GetStringRes(IDS_SETTINGS_FONT_COPYTOOTHERS), [this, &item, family, get] {
				if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.FormatStringRes(IDS_SETTINGS_FONT_CONFIRMCOPY, Wide(family))) != IDYES)
					return;
				// Only the sources; whether the others are on, and their digits, stay as they are.
				auto all = item.Value();
				for (const auto& other : m_fontFamilies) {
					if (other == family)
						continue;
					auto& settings = all[other];
					settings.Sources = get();
					if (settings == FontReplacementFamily{})
						all.erase(other);
				}
				item = std::move(all);
			}},
			{{}, {}},
			{rt.GetStringRes(IDS_SETTINGS_FONT_OPENPRESETFOLDER), [this] { OpenInExplorer(m_hWndOwner, GetFontPresetFolder()); }},
		});

	CreateChoiceRow(rt.GetStringRes(IDS_SETTINGS_FONT_MONOSPACEDDIGITS), true, [getFamily] { return getFamily().MonospacedDigits; }, [getFamily, setFamily] {
		auto settings = getFamily();
		settings.MonospacedDigits = !settings.MonospacedDigits;
		setFamily(std::move(settings));
	}, true);

	auto& grid = CreateGridRow({}, IDS_SETTINGS_FONT_SOURCES_DESC, 200, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_SOURCE_TYPE), .Kind = GridView::CellKind::ReadOnly, .Width = 100},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Kind = GridView::CellKind::ComboEdit, .Width = 220, .GetChoices = [this, get](size_t row) {
			return get()[row].IsPreset() ? ListPresets() : ListSystemFontFamilies();
		}},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_WEIGHT), .Kind = GridView::CellKind::ComboChoice, .Width = 120, .GetChoices = [](size_t) { return NamedValues(FontWeights); }},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_STRETCH), .Kind = GridView::CellKind::ComboChoice, .Width = 130, .GetChoices = [](size_t) { return NamedValues(FontStretches); }},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_STYLE), .Kind = GridView::CellKind::ComboChoice, .Width = 90, .GetChoices = [](size_t) { return NamedValues(FontStyles); }},
	}, {
		.GetRowCount = [get] { return get().size(); },
		.GetText = [&rt, get](size_t row, size_t column) -> std::wstring {
			const auto& source = get()[row];
			switch (column) {
				case 1:
					return rt.GetStringRes(source.IsPreset() ? IDS_SETTINGS_FONT_SOURCE_PRESET : IDS_SETTINGS_FONT_SOURCE_FONT);
				case 2:
					return Wide(source.IsPreset() ? source.Preset : source.Font.Name);
				case 3:
					return source.IsPreset() ? std::wstring() : NamedValue(FontWeights, source.Font.Weight);
				case 4:
					return source.IsPreset() ? std::wstring() : NamedValue(FontStretches, source.Font.Stretch);
				case 5:
					return source.IsPreset() ? std::wstring() : NamedValue(FontStyles, source.Font.Style);
				default:
					return {};
			}
		},
		.GetChecked = [get](size_t row, size_t) { return get()[row].Enabled; },
		.IsEditable = [get](size_t row, size_t column) { return column <= 2 || !get()[row].IsPreset(); },
		.SetText = [edit, parseNumber](size_t row, size_t column, const std::wstring& text) {
			const auto value = xivres::util::trim(text);
			if (column == 2 && value.empty())
				return false;
			edit(row, [&](FontReplacementFamilySource& source) {
				switch (column) {
					case 2:
						(source.IsPreset() ? source.Preset : source.Font.Name) = Utf8(value);
						break;
					case 3:
						source.Font.Weight = parseNumber(value);
						break;
					case 4:
						source.Font.Stretch = parseNumber(value);
						break;
					case 5:
						source.Font.Style = parseNumber(value);
						break;
				}
			});
			return true;
		},
		.SetChecked = [edit](size_t row, size_t, bool checked) {
			edit(row, [checked](FontReplacementFamilySource& source) { source.Enabled = checked; });
		},
		.MoveRow = [get, set](size_t from, size_t to) {
			auto sources = get();
			MoveWithin(sources, from, to);
			set(std::move(sources));
		},
		.ShowContextMenu = [this, get, remove](size_t row, POINT ptScreen) {
			if (row >= get().size())
				return;
			const auto preset = get()[row].Preset;
			enum : UINT { IdRemove = 1, IdShow };
			const auto hMenu = CreatePopupMenu();
			const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
			AppendMenuW(hMenu, MF_STRING, IdRemove, m_config->Runtime.GetStringRes(IDS_SETTINGS_REMOVE));
			AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(hMenu, MF_STRING | (preset.empty() ? MF_GRAYED : 0), IdShow, m_config->Runtime.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER));
			switch (TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr)) {
				case IdRemove:
					remove();
					break;
				case IdShow:
					OpenInExplorer(m_hWndOwner, FontReplacement::Presets::Resolve(GetFontPresetFolder(), preset));
					break;
			}
		},
		.KeyDown = [remove](UINT vk) {
			if (vk != VK_DELETE)
				return false;
			remove();
			return true;
		},
	});
	*pGrid = &grid;
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });

	// The preview shows the edge, as it is set on the font replacement's page.
	m_rowCleanup += rt.FontReplacement.Edge.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
	AddFontPreviewRows(family, false);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddFontPreviewRows(const std::string& family, bool edgePage) {
	// Without the game's font table there is no face to draw with.
	auto faces = FontReplacement::GameFontNames::FacesOf(family);
	if (faces.empty())
		return;
	auto& rt = m_config->Runtime;

	// The family's sizes are offered; any other can be typed. The size last chosen, or the one nearest the size most text
	// is drawn at.
	std::vector<float> sizes;
	for (const auto size : faces | std::views::values) {
		if (std::ranges::find(sizes, size) == sizes.end())
			sizes.push_back(size);
	}
	std::ranges::sort(sizes);
	if (auto& chosen = m_fontPreviewSizes[family]; chosen <= 0)
		chosen = std::ranges::min(sizes, {}, [](float size) { return std::fabs(size - 14.f); });
	const auto textKey = FontPreviewTextKey(family);
	if (auto& text = m_fontPreviewTexts[textKey]; text.empty())
		text = textKey == MeidingerFamily ? MeidingerPreviewText : DefaultFontPreviewText;
	m_fontPreview = std::make_unique<FontPreview>(FontPreview{.Family = family, .Faces = std::move(faces)});

	// The size and what goes with it share a line, the size box as wide as a number needs.
	constexpr int LineGroup = 1;
	auto pSizeRow = std::make_unique<Row>();
	auto& sizeRow = *pSizeRow;
	sizeRow.Type = Row::RowType::ComboEdit;
	sizeRow.LineGroup = LineGroup;
	sizeRow.LineWidth = 80;
	sizeRow.Label = CreateRowControl(PartLabel, 0, WC_STATICW, rt.GetStringRes(IDS_SETTINGS_FONT_PREVIEW_SIZE), SS_LEFT | SS_NOPREFIX);
	sizeRow.Control = CreateRowControl(PartControl, 0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL);
	for (const auto size : sizes)
		ComboBox_AddString(sizeRow.Control, FormatNumber(size).c_str());
	sizeRow.Refresh = [this, &sizeRow, family] { SetWindowTextW(sizeRow.Control, FormatNumber(m_fontPreviewSizes[family]).c_str()); };
	// Drawn as it is typed, once typing stops for a while. A size is drawn as the replacement draws it: rounded to half
	// pixels, from 4 to 255 px; what isn't a size is left alone while it is typed, and shown as it was once the box is left.
	sizeRow.CommitOnChange = true;
	sizeRow.Commit = [this, &sizeRow, family] {
		if (const auto value = ParseNumber<float>(GetText(sizeRow.Control)); value && std::isfinite(*value) && *value > 0) {
			const auto size = std::round(std::clamp(*value, MinFontPreviewSize, MaxFontPreviewSize) * 2) / 2;
			if (auto& chosen = m_fontPreviewSizes[family]; chosen != size) {
				chosen = size;
				RequestFontPreview(FontPreviewDelay);
			}
		}
		if (!IsTypingInto(sizeRow, GetFocus()))
			sizeRow.Refresh();
	};
	sizeRow.Click = [this, &sizeRow, family, sizes](int part) {
		// One of the family's sizes was picked from the list.
		if (const auto selected = ComboBox_GetCurSel(sizeRow.Control); part == PartControl && selected >= 0 && static_cast<size_t>(selected) < sizes.size()) {
			m_fontPreviewSizes[family] = sizes[selected];
			RequestFontPreview(std::chrono::milliseconds(0));
		}
	};
	m_rows.emplace_back(std::move(pSizeRow));

	if (edgePage) {
		// The colors of every page's preview, for the session.
		std::vector<std::wstring> colorChoices;
		for (const auto& preset : FontPreviewColorPresets)
			colorChoices.emplace_back(rt.GetStringRes(preset.LabelId));
		CreateComboRow(rt.GetStringRes(IDS_SETTINGS_FONT_PREVIEW_COLORS), colorChoices, [this] { return static_cast<int>(m_fontPreviewColors); }, [this](int index) {
			if (index >= 0 && static_cast<size_t>(index) < std::size(FontPreviewColorPresets)) {
				m_fontPreviewColors = static_cast<size_t>(index);
				RequestFontPreview(std::chrono::milliseconds(0));
			}
		});
		m_rows.back()->LineGroup = LineGroup;
	} else {
		// Drawn as it is typed, once typing stops for a while; as the TexTools ModPacks' filter, it is not set back.
		CreateNumberRow(rt.GetStringRes(IDS_SETTINGS_FONT_PREVIEW_TEXT), {}, {});
		auto& textRow = *m_rows.back();
		textRow.LineGroup = LineGroup;
		textRow.Refresh = nullptr;
		textRow.Commit = [this, &textRow, textKey] {
			m_fontPreviewTexts[textKey] = GetText(textRow.Control);
			RequestFontPreview(FontPreviewDelay);
		};
		// Setting the text tells of a change at once; it is applied only after.
		SetWindowTextW(textRow.Control, m_fontPreviewTexts[textKey].c_str());
		textRow.CommitOnChange = true;
	}

	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Preview;
	row.Height = 140;
	row.Description = CreateRowControl(PartDescription, 0, WC_STATICW, edgePage
		? rt.FormatStringRes(IDS_SETTINGS_EDGE_PREVIEW_DESC, Wide(family))
		: std::wstring(rt.GetStringRes(IDS_SETTINGS_FONT_PREVIEW_DESC)), SS_LEFT | SS_NOPREFIX);
	row.Control = CreateRowControl(PartControl, 0, FontPreviewClassName, L"", 0);
	SetWindowLongPtrW(row.Control, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	// Asked for again whenever the page's rows are refreshed, as they are when the sources change; the faces are kept if
	// the sources are the same, so that costs only drawing the text.
	row.Refresh = [this] { RequestFontPreview(FontPreviewDelay); };
	m_fontPreview->Control = row.Control;
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::RequestFontPreview(std::chrono::milliseconds delay) {
	if (!m_fontPreview)
		return;

	// Before the page is laid out, LayoutPage asks once it is.
	RECT rc;
	GetClientRect(m_fontPreview->Control, &rc);
	if (rc.right <= 0 || rc.bottom <= 0)
		return;

	// The page's first image is wanted at once; the previewer posts here, and whether the image is still wanted is told
	// when it is taken.
	if (!m_fontPreview->Generation)
		delay = std::chrono::milliseconds(0);
	if (!m_fontPreviewer)
		m_fontPreviewer = std::make_unique<FontReplacement::FamilyPreview>([hWnd = m_hWnd](uint64_t) { PostMessageW(hWnd, WmFontPreviewDrawn, 0, 0); });

	const auto& settings = m_config->Runtime.FontReplacement.Faces;
	const auto& family = m_fontPreview->Family;
	FontReplacementFamily familySettings;
	if (const auto& all = settings.Families.Value(); all.contains(family))
		familySettings = all.at(family);
	// The face the game picks for the size, taken to be the nearest (the larger of two as near), scaled to the size.
	const auto size = m_fontPreviewSizes[family];
	const auto& face = std::ranges::min(m_fontPreview->Faces, [size](const auto& l, const auto& r) {
		const auto dl = std::fabs(l.second - size), dr = std::fabs(r.second - size);
		return dl < dr || (dl == dr && l.second > r.second);
	});
	const auto& colors = FontPreviewColorPresets[std::min(m_fontPreviewColors, std::size(FontPreviewColorPresets) - 1)];
	m_fontPreview->Requested = {rc.right, rc.bottom};
	m_fontPreview->Generation = m_fontPreviewer->Request({
		.Family = family,
		.FaceName = face.first,
		.Size = size,
		.Settings = std::move(familySettings),
		.PresetFolder = GetFontPresetFolder(),
		.Reload = std::exchange(m_fontPreview->Reload, false),
		.Edge = m_config->Runtime.FontReplacement.Edge.Value(),
		.Text = m_fontPreviewTexts[FontPreviewTextKey(family)],
		.Width = static_cast<int>(rc.right),
		.Height = static_cast<int>(rc.bottom),
		.Foreground = colors.Text,
		.EdgeColor = colors.Edge,
		.Background = colors.Background,
	}, delay);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::OnFontPreviewDrawn() {
	if (!m_fontPreview || !m_fontPreviewer)
		return;
	if (auto image = m_fontPreviewer->Take(m_fontPreview->Generation)) {
		m_fontPreview->Image = std::move(*image);
		InvalidateRect(m_fontPreview->Control, nullptr, FALSE);
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::PaintFontPreview(HWND hwnd) {
	PAINTSTRUCT ps{};
	const auto hdc = BeginPaint(hwnd, &ps);
	RECT rc;
	GetClientRect(hwnd, &rc);
	const auto& colors = GetThemeColors(IsDarkModeEnabled());

	// The image at the top left, which is of the size it was asked for; the page's background around it, until it is
	// drawn anew at the new size.
	const auto pImage = m_fontPreview && m_fontPreview->Control == hwnd && m_fontPreview->Image ? &*m_fontPreview->Image : nullptr;
	auto drawnWidth = 0, drawnHeight = 0;
	if (pImage && pImage->Pixels.size() == static_cast<size_t>(pImage->Width) * pImage->Height) {
		BITMAPINFO bmi{.bmiHeader = {
			.biSize = sizeof(BITMAPINFOHEADER),
			.biWidth = pImage->Width,
			.biHeight = -pImage->Height,
			.biPlanes = 1,
			.biBitCount = 32,
			.biCompression = BI_RGB,
		}};
		SetDIBitsToDevice(hdc, 0, 0, pImage->Width, pImage->Height, 0, 0, 0, pImage->Height, pImage->Pixels.data(), &bmi, DIB_RGB_COLORS);
		drawnWidth = pImage->Width;
		drawnHeight = pImage->Height;
	}
	const RECT rcRight{drawnWidth, 0, rc.right, rc.bottom};
	const RECT rcBelow{0, drawnHeight, std::min<LONG>(drawnWidth, rc.right), rc.bottom};
	for (const auto& r : {rcRight, rcBelow}) {
		if (r.left < r.right && r.top < r.bottom)
			FillRect(hdc, &r, *m_backgroundBrush);
	}

	// What couldn't be read, and why the edge isn't drawn, along the bottom, in the text's color: the image's background
	// is the preview's colors', not the page's.
	auto failures = pImage ? Wide(pImage->Failures) : std::wstring();
	if (pImage && !pImage->EdgeFailure.empty())
		failures += (failures.empty() ? L"" : L"\n") + m_config->Runtime.FormatStringRes(IDS_SETTINGS_FONT_PREVIEW_NOEDGE, Wide(pImage->EdgeFailure));
	if (!failures.empty()) {
		const auto& text = failures;
		const auto hPrevFont = SelectObject(hdc, *m_font);
		const auto margin = static_cast<int>(8 * GetZoom());
		RECT rcText{margin, 0, std::max<LONG>(margin + 1, rc.right - margin), 0};
		DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		const auto height = rcText.bottom;
		rcText = {margin, std::max<LONG>(0, rc.bottom - margin - height), rc.right - margin, rc.bottom - margin};
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, FontPreviewColorPresets[std::min(m_fontPreviewColors, std::size(FontPreviewColorPresets) - 1)].Text);
		DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		SelectObject(hdc, hPrevFont);
	}

	FrameRect(hdc, &rc, *colors.CreateBackgroundWeakBrush());
	EndPaint(hwnd, &ps);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateChoiceRow(const std::wstring& label, bool enabled, std::function<bool()> isChosen, std::function<void()> choose, bool checkBox, int flowGroup) {
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Check;
	row.FlowGroup = flowGroup;
	row.Control = CreateRowControl(PartControl, 0, WC_BUTTONW, EscapeMnemonics(label), WS_TABSTOP | (checkBox ? BS_CHECKBOX : BS_RADIOBUTTON) | BS_MULTILINE | BS_TOP | BS_NOTIFY);
	EnableWindow(row.Control, enabled);
	row.Refresh = [&row, isChosen = std::move(isChosen)] { Button_SetCheck(row.Control, isChosen() ? BST_CHECKED : BST_UNCHECKED); };
	row.Click = [this, choose = std::move(choose)](int) {
		choose();
		RefreshRows();
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::OpenPatchCodeEditor(const std::wstring& name, const std::filesystem::path& path) {
	std::erase_if(m_patchCodeEditors, [](const auto& editor) { return editor->IsDestroyed(); });
	for (const auto& editor : m_patchCodeEditors) {
		if (editor->GetPath() == path) {
			SetForegroundWindow(editor->Handle());
			return;
		}
	}
	m_patchCodeEditors.emplace_back(std::make_unique<ConfigWindow>(m_config->Runtime.FormatStringRes(IDS_SETTINGS_PATCHCODE_EDITORTITLE, name), path));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreatePatchCode() {
	try {
		const auto& directory = m_config->PatchCode.GetDirectory();
		create_directories(directory);

		const std::wstring name = m_config->Runtime.GetStringRes(IDS_SETTINGS_PATCHCODE_DEFAULTNAME);
		auto path = directory / std::format(L"{}.json", name);
		for (auto i = 2; exists(path); ++i)
			path = directory / std::format(L"{} ({}).json", name, i);

		// With its key made now, so that loading it does not write to the file being edited.
		PatchInstruction patch{.Name = Utf8(name)};
		patch.CreateNewHmacKeyIfInvalid();
		Utils::SaveJsonToFile(path, patch);
		m_config->PatchCode.Reload();

		OpenPatchCodeEditor(path.filename().wstring(), path);
	} catch (const std::exception& e) {
		Dll::MessageBoxF(m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
	}
}

HWND XivAlexander::Apps::MainApp::Window::SettingsView::CreateRowControl(int part, DWORD exStyle, LPCWSTR className, const std::wstring& text, DWORD style) {
	// For the row being created, which is the next one.
	const auto id = FirstRowControlId + static_cast<int>(m_rows.size()) * ControlsPerRow + part;
	const auto hWnd = CreateWindowExW(exStyle, className, text.c_str(), WS_CHILD | WS_VISIBLE | style,
		0, 0, 0, 0, m_hPage, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), Dll::Module(), nullptr);
	SendMessageW(hWnd, WM_SETFONT, reinterpret_cast<WPARAM>(*m_font), FALSE);
	ApplyThemeToControl(hWnd);
	return hWnd;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateHeadingRow(const std::wstring& text) {
	auto pRow = std::make_unique<Row>();
	pRow->Type = Row::RowType::Heading;
	pRow->Label = CreateRowControl(PartLabel, 0, WC_STATICW, text, SS_LEFT | SS_NOPREFIX);
	SendMessageW(pRow->Label, WM_SETFONT, reinterpret_cast<WPARAM>(*m_boldFont), FALSE);
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateRichTextRow(const std::wstring& text) {
	static const auto hRichEdit = LoadLibraryW(L"Msftedit.dll");
	if (!hRichEdit) {
		CreateNoteRow(text);
		return;
	}

	auto& rt = m_config->Runtime;
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::RichText;
	row.Description = CreateRowControl(PartDescription, 0, MSFTEDIT_CLASS, L"", ES_MULTILINE | ES_READONLY | ES_NOHIDESEL);
	row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_SHOWMORE), WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY);

	// Paragraphs as the control takes them, and links told to the page.
	std::wstring normalized;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
			continue;
		if (text[i] == L'\n' || text[i] == L'\r')
			normalized += L"\r\n";
		else
			normalized += text[i];
	}
	SendMessageW(row.Description, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);
	SendMessageW(row.Description, EM_SETEVENTMASK, 0, ENM_LINK);
	SetWindowTextW(row.Description, normalized.c_str());

	// In the theme's colors, which it doesn't take from the page.
	row.Refresh = [this, &row] {
		const auto& colors = GetThemeColors(IsDarkModeEnabled());
		SendMessageW(row.Description, EM_SETBKGNDCOLOR, 0, colors.GetBackground());
		CHARFORMAT2W format{};
		format.cbSize = sizeof format;
		format.dwMask = CFM_COLOR;
		format.crTextColor = colors.GetForeground();
		SendMessageW(row.Description, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
	};
	row.Click = [this, &row](int part) {
		if (part != PartButton1)
			return;
		row.Expanded = !row.Expanded;
		SetWindowTextW(row.Button1, m_config->Runtime.GetStringRes(row.Expanded ? IDS_SETTINGS_SHOWLESS : IDS_SETTINGS_SHOWMORE));
		LayoutPage();
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateEnabledRow(std::function<bool()> isChecked, std::function<void()> toggle, std::vector<std::pair<std::wstring, std::function<void()>>> actions) {
	// Whether the page's folder, pack, or family is used, with what can be done with it in a menu at the line's end.
	auto& rt = m_config->Runtime;
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Check;
	row.Control = CreateRowControl(PartControl, 0, WC_BUTTONW, EscapeMnemonics(rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED)), WS_TABSTOP | BS_CHECKBOX | BS_MULTILINE | BS_TOP | BS_NOTIFY);
	if (!actions.empty())
		row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_EDITMENU), WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY);
	row.Refresh = [&row, isChecked = std::move(isChecked)] { Button_SetCheck(row.Control, isChecked() ? BST_CHECKED : BST_UNCHECKED); };
	row.Click = [this, &row, toggle = std::move(toggle), actions = std::move(actions)](int part) {
		if (part != PartButton1) {
			toggle();
			RefreshRows();
			return;
		}

		// Below the button, at its right edge, as a drop-down's list; an action without a label is a separator.
		RECT rc{};
		GetWindowRect(row.Button1, &rc);
		const auto hMenu = CreatePopupMenu();
		const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
		for (size_t i = 0; i < actions.size(); ++i) {
			if (actions[i].first.empty())
				AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			else
				AppendMenuW(hMenu, MF_STRING, i + 1, actions[i].first.c_str());
		}
		const auto chosen = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, rc.right, rc.bottom, 0, m_hWnd, nullptr);
		if (chosen > 0 && static_cast<size_t>(chosen) <= actions.size() && actions[chosen - 1].second)
			actions[chosen - 1].second();
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTabbedRows() {
	// The page's lists, each in a tab; the tab chosen last for them is chosen again.
	if (m_deferredTabbed.empty())
		return;
	const auto items = std::exchange(m_deferredTabbed, {});

	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Tabs;
	const auto key = items.front();
	row.ActiveTab = std::clamp(m_activeTabs[key], 0, static_cast<int>(items.size()) - 1);
	for (size_t i = 0; i < items.size(); ++i) {
		const auto hButton = CreateWindowExW(0, WC_BUTTONW, EscapeMnemonics(GetItemLabel(*items[i])).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_RADIOBUTTON | BS_PUSHLIKE | BS_NOTIFY,
			0, 0, 0, 0, m_hPage, reinterpret_cast<HMENU>(static_cast<INT_PTR>(FirstTabControlId + i)), Dll::Module(), nullptr);
		SendMessageW(hButton, WM_SETFONT, reinterpret_cast<WPARAM>(*m_font), FALSE);
		ApplyThemeToControl(hButton);
		row.Tabs.push_back(hButton);
	}
	row.Refresh = [&row] {
		for (size_t i = 0; i < row.Tabs.size(); ++i)
			Button_SetCheck(row.Tabs[i], static_cast<int>(i) == row.ActiveTab ? BST_CHECKED : BST_UNCHECKED);
	};
	row.Click = [this, &row, key](int tab) {
		row.ActiveTab = tab;
		m_activeTabs[key] = tab;
		row.Refresh();
		LayoutPage();
	};
	const auto pOwner = pRow.get();
	m_rows.emplace_back(std::move(pRow));

	// A list in a tab goes without its label, which is the tab's.
	for (size_t i = 0; i < items.size(); ++i) {
		const auto first = m_rows.size();
		CreateRow(*items[i]);
		for (auto r = first; r < m_rows.size(); ++r) {
			m_rows[r]->TabOwner = pOwner;
			m_rows[r]->TabIndex = static_cast<int>(i);
			if (r == first && m_rows[r]->Type == Row::RowType::Grid && m_rows[r]->Label) {
				DestroyWindow(m_rows[r]->Label);
				m_rows[r]->Label = nullptr;
			}
		}
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateHeadingRow(const std::wstring& text, const std::wstring& checkLabel, bool enabled, std::function<bool()> isChecked, std::function<void()> toggle,
	const std::wstring& linkLabel, std::function<void()> link) {
	// A heading with a check box right after it, for a choice about the whole section.
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Heading;
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, text, SS_LEFT | SS_NOPREFIX);
	SendMessageW(row.Label, WM_SETFONT, reinterpret_cast<WPARAM>(*m_boldFont), FALSE);
	row.Control = CreateRowControl(PartControl, 0, WC_BUTTONW, EscapeMnemonics(checkLabel), WS_TABSTOP | BS_CHECKBOX | BS_NOTIFY);
	EnableWindow(row.Control, enabled);
	if (link) {
		// At the line's end, drawn as a link (WM_DRAWITEM) for an action lighter than a button's.
		row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, linkLabel, WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY);
		row.LinkButton = true;
	}
	row.Refresh = [&row, isChecked = std::move(isChecked)] { Button_SetCheck(row.Control, isChecked() ? BST_CHECKED : BST_UNCHECKED); };
	row.Click = [this, toggle = std::move(toggle), link = std::move(link)](int part) {
		if (part == PartButton1) {
			if (link)
				link();
			return;
		}
		toggle();
		RefreshRows();
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateNoteRow(const std::wstring& text) {
	auto pRow = std::make_unique<Row>();
	pRow->Type = Row::RowType::Note;
	pRow->Description = CreateRowControl(PartDescription, 0, WC_STATICW, text, SS_LEFT | SS_NOPREFIX);
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateActionRow(std::wstring label1, std::function<void()> onClick1, std::wstring label2, std::function<void()> onClick2) {
	auto pRow = std::make_unique<Row>();
	pRow->Type = Row::RowType::Action;
	pRow->Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, label1, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY);
	if (onClick2)
		pRow->Button2 = CreateRowControl(PartButton2, 0, WC_BUTTONW, label2, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY);
	pRow->Click = [onClick1 = std::move(onClick1), onClick2 = std::move(onClick2)](int part) {
		if (part == PartButton1)
			onClick1();
		else if (part == PartButton2 && onClick2)
			onClick2();
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateActionRow(std::vector<std::pair<std::wstring, std::function<void()>>> buttons) {
	auto pRow = std::make_unique<Row>();
	pRow->Type = Row::RowType::Action;
	const std::pair<int, HWND Row::*> slots[]{{PartButton1, &Row::Button1}, {PartButton2, &Row::Button2}, {PartButton3, &Row::Button3}, {PartButton4, &Row::Button4}};
	std::map<int, std::function<void()>> clicks;
	for (size_t i = 0; i < buttons.size() && i < std::size(slots); ++i) {
		const auto [part, slot] = slots[i];
		(*pRow).*slot = CreateRowControl(part, 0, WC_BUTTONW, buttons[i].first, WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY);
		clicks.emplace(part, std::move(buttons[i].second));
	}
	pRow->Click = [clicks = std::move(clicks)](int part) {
		if (const auto it = clicks.find(part); it != clicks.end() && it->second)
			it->second();
	};
	m_rows.emplace_back(std::move(pRow));
}

bool XivAlexander::Apps::MainApp::Window::SettingsView::CreateSpecialRows(const ConfigItemBase& item) {
	auto& rt = m_config->Runtime;
	const std::pair<const ConfigItemBase*, std::function<void()>> pages[]{
		{&rt.Modding.PathReplacements, [this] { AddPathReplacementRows(); }},
		{&rt.Modding.Logging.PathFilters, [this] { AddPathFilterRows(); }},
		{&rt.Modding.Languages.FallbackPriority, [this] { AddFallbackPriorityRows(); }},
		{&rt.Modding.Languages.ForcedCharacterLanguages, [this] { AddForcedLanguageRows(); }},
		{&rt.Modding.Ttmp.ChoicesFiles, [this] { AddChoicesFileRows(); }},
		{&rt.FontReplacement.Edge, [this] { AddEdgeRows(); }},
		{&rt.Modding.AdditionalSqpackRootDirectories, [this, &rt] { AddDirectoryListRows(rt.Modding.AdditionalSqpackRootDirectories, {}, 0); }},
		{&rt.Modding.GameResourceFileEntryRootDirectories, [this, &rt] {
			AddDirectoryListRows(rt.Modding.GameResourceFileEntryRootDirectories, rt.Modding.DefaultGameResourceFileEntryRootDirectories(), IDS_SETTINGS_DIRS_DESC);
		}},
		{&rt.Modding.Ttmp.SearchDirectories, [this, &rt] {
			AddDirectoryListRows(rt.Modding.Ttmp.SearchDirectories, rt.Modding.DefaultTtmpSearchDirectories(), IDS_SETTINGS_DIRS_DESC);
		}},
	};
	for (const auto& [special, add] : pages) {
		if (special == &item) {
			add();
			return true;
		}
	}
	return false;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::CreateRow(ConfigItemBase& item) {
	auto& rt = m_config->Runtime;
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Item = &item;

	const auto menuText = [this](UINT commandId) { return GetMenuText(IDR_TRAY_MENU, commandId); };
	const auto gameLanguageChoice = [&rt](xivres::game_language language) {
		return std::pair{language, language == xivres::game_language::Unspecified
			? rt.GetLanguageNameLocalized(language)
			: std::format(L"{} ({})", rt.GetLanguageNameLocalized(language), xivres::util::unicode::convert<std::wstring>(xivres::game_language_code(language)))};
	};

	if (CreateSpecialRows(item))
		return;

	if (const auto info = FindItemInfo(item))
		row.PickFiles = info->PickFiles;

	if (&item == &rt.Modding.Languages.LipSyncLanguage) {
		// Only the languages the game has lip sync of; the item holds no others.
		BindEnum<xivres::game_language>(row, [&gameLanguageChoice] {
			std::vector<std::pair<xivres::game_language, std::wstring>> res{gameLanguageChoice(xivres::game_language::Unspecified)};
			for (const auto language : RuntimeConfigRepository::ModdingGroup::LanguagesGroup::LipSyncLanguages)
				res.emplace_back(gameLanguageChoice(language));
			return res;
		});
	}

	if (&item == &rt.Audio.OutputSamplingRate) {
		// As the menu offers them.
		BindNumberCombo<uint32_t>(row, [&rt] {
			using Features::AudioResampler;
			std::vector<std::pair<uint32_t, std::wstring>> res;
			const auto deviceRate = AudioResampler::DefaultDeviceRate();
			res.emplace_back(AudioResampler::MatchDefaultDevice, CleanMenuText(deviceRate
				? rt.FormatStringRes(IDS_MENU_SAMPLINGRATE_MATCHDEVICE, deviceRate)
				: std::wstring(rt.GetStringRes(IDS_MENU_SAMPLINGRATE_MATCHDEVICE_UNKNOWN))));
			for (size_t i = 0; i < std::size(AudioResampler::Choices); i++) {
				const auto rate = AudioResampler::Choices[i];
				res.emplace_back(rate, CleanMenuText(rt.FormatStringRes(
					rate == AudioResampler::GameDefault ? IDS_MENU_SAMPLINGRATE_RATE_DEFAULT : IDS_MENU_SAMPLINGRATE_RATE,
					rate, i + 1)));
			}
			return res;
		});
	}

	void(row.Type != Row::RowType::Unsupported
		|| BindBool(row)
		|| BindNumber<int>(row)
		|| BindNumber<int64_t>(row)
		|| BindNumber<uint32_t>(row)
		|| BindNumber<uint64_t>(row)
		|| BindNumber<double>(row)
		|| BindHex16(row)
		|| BindString(row)
		|| BindPath(row)
		|| BindPathList(row)
		|| BindEnum<Language>(row, [&] {
			return std::vector<std::pair<Language, std::wstring>>{
				{Language::SystemDefault, menuText(ID_CONFIGURE_LANGUAGE_SYSTEMDEFAULT)},
				{Language::English, menuText(ID_CONFIGURE_LANGUAGE_ENGLISH)},
				{Language::Korean, menuText(ID_CONFIGURE_LANGUAGE_KOREAN)},
				{Language::Japanese, menuText(ID_CONFIGURE_LANGUAGE_JAPANESE)},
			};
		})
		|| BindEnum<ThemeMode>(row, [&] {
			return std::vector<std::pair<ThemeMode, std::wstring>>{
				{ThemeMode::System, menuText(ID_CONFIGURE_THEME_SYSTEM)},
				{ThemeMode::Light, menuText(ID_CONFIGURE_THEME_LIGHT)},
				{ThemeMode::Dark, menuText(ID_CONFIGURE_THEME_DARK)},
			};
		})
		|| BindEnum<GameWindowTitleMode>(row, [&] {
			return std::vector<std::pair<GameWindowTitleMode, std::wstring>>{
				{GameWindowTitleMode::None, menuText(ID_CONFIGURE_WINDOWTITLE_PID_NONE)},
				{GameWindowTitleMode::Prefix, menuText(ID_CONFIGURE_WINDOWTITLE_PID_PREFIX)},
				{GameWindowTitleMode::Suffix, menuText(ID_CONFIGURE_WINDOWTITLE_PID_SUFFIX)},
			};
		})
		|| BindEnum<HighLatencyMitigationMode>(row, [&] {
			return std::vector<std::pair<HighLatencyMitigationMode, std::wstring>>{
				{HighLatencyMitigationMode::SubtractLatency, menuText(ID_NETWORK_HIGHLATENCYMITIGATION_MODE_1)},
				{HighLatencyMitigationMode::SimulateRtt, CleanMenuText(rt.FormatStringRes(IDS_MENU_NETWORKLATENCYHANDLEMODE_2, rt.NetworkTiming.ExpectedAnimationLockDurationUs.Value()))},
				{HighLatencyMitigationMode::SimulateNormalizedRttAndLatency, menuText(ID_NETWORK_HIGHLATENCYMITIGATION_MODE_3)},
			};
		})
		|| BindEnum<xivres::game_language>(row, [&gameLanguageChoice] {
			std::vector<std::pair<xivres::game_language, std::wstring>> res;
			for (auto i = 0; i <= static_cast<int>(xivres::game_language::TraditionalChinese); ++i)
				res.emplace_back(gameLanguageChoice(static_cast<xivres::game_language>(i)));
			return res;
		})
		|| BindEnum<xivres::game_publisher>(row, [&rt] {
			std::vector<std::pair<xivres::game_publisher, std::wstring>> res;
			for (auto i = 0; i <= static_cast<int>(xivres::game_publisher::UserjoyGames); ++i)
				res.emplace_back(static_cast<xivres::game_publisher>(i), rt.GetRegionNameLocalized(static_cast<xivres::game_publisher>(i)));
			return res;
		})
		|| BindEnum<SoxrResamplerConfig::QualityPreset>(row, [] {
			using Q = SoxrResamplerConfig::QualityPreset;
			return NamedChoices({Q::Quick, Q::Low, Q::Medium, Q::Bits16, Q::Bits20, Q::Bits24, Q::Bits28, Q::Bits32});
		})
		|| BindEnum<SoxrResamplerConfig::PhaseResponse>(row, [] {
			using P = SoxrResamplerConfig::PhaseResponse;
			return NamedChoices({P::Linear, P::Intermediate, P::Minimum});
		})
		|| BindEnum<SoxrResamplerConfig::Rolloff>(row, [] {
			using R = SoxrResamplerConfig::Rolloff;
			return NamedChoices({R::Small, R::Medium, R::None});
		}));

	if (row.Type == Row::RowType::Unsupported) {
		row.Click = [this](int part) {
			if (part == PartButton1)
				PostMessageW(m_hWndOwner, WM_COMMAND, ID_CONFIGURE_EDITRUNTIMECONFIGURATION, 0);
		};
	}

	const auto create = [this](DWORD exStyle, LPCWSTR className, const std::wstring& text, DWORD style, int part) {
		return CreateRowControl(part, exStyle, className, text, style);
	};

	auto label = GetItemLabel(item);
	if (const auto info = FindItemInfo(item); info && info->VersionSensitive && rt.AreVersionSensitiveFeaturesDisabledTemporarily())
		label = L"(!) " + label;
	if (row.Type != Row::RowType::Check)
		row.Label = create(0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX, PartLabel);

	UINT descriptionId = 0;
	if (const auto info = FindItemInfo(item))
		descriptionId = info->DescriptionId;
	if (row.Type == Row::RowType::Unsupported)
		descriptionId = IDS_SETTINGS_EDITINCONFIGEDITOR;
	if (descriptionId)
		row.Description = create(0, WC_STATICW, rt.GetStringRes(descriptionId), SS_LEFT | SS_NOPREFIX, PartDescription);

	constexpr DWORD ButtonStyle = WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY;
	switch (row.Type) {
		case Row::RowType::Check:
			// With its label, so that the focus is drawn around it.
			row.Control = create(0, WC_BUTTONW, EscapeMnemonics(label), WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE | BS_TOP | BS_NOTIFY, PartControl);
			break;

		case Row::RowType::Edit:
			row.Control = create(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL, PartControl);
			break;

		case Row::RowType::Combo:
			row.Control = create(0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, PartControl);
			for (const auto& choice : row.ComboLabels)
				ComboBox_AddString(row.Control, choice.c_str());
			break;

		case Row::RowType::ComboEdit:
			row.Control = create(0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, PartControl);
			for (const auto& choice : row.ComboLabels)
				ComboBox_AddString(row.Control, choice.c_str());
			break;

		case Row::RowType::PathEdit:
			row.Control = create(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL, PartControl);
			row.Button1 = create(0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_BROWSE), ButtonStyle, PartButton1);
			break;

		case Row::RowType::PathList:
			row.Control = create(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, PartControl);
			row.Button1 = create(0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_ADD), ButtonStyle, PartButton1);
			row.Button2 = create(0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_REMOVE), ButtonStyle, PartButton2);
			break;

		case Row::RowType::Unsupported:
			row.Button1 = create(0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_OPENEDITOR), ButtonStyle, PartButton1);
			break;

		default:
			break;
	}

	// Changes from anywhere, the menus included, show here.
	m_rowCleanup += item.OnChange([this] { PostMessageW(m_hWnd, WmRefreshRows, 0, 0); });
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::RefreshRows() {
	const auto hFocus = GetFocus();
	for (const auto& row : m_rows) {
		// Not over what is being typed.
		if (row->Refresh && !IsTypingInto(*row, hFocus))
			row->Refresh();
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::LayoutPage() {
	if (!m_hPage)
		return;

	RECT rcPage;
	GetClientRect(m_hPage, &rcPage);
	const auto zoom = GetZoom();
	const auto scale = [zoom](double v) { return static_cast<int>(v * zoom); };
	const auto pad = scale(12);
	const auto gap = scale(8);
	const auto rowGap = scale(12);
	const auto controlHeight = scale(24);
	const auto buttonWidth = scale(96);
	const auto checkWidth = scale(20);
	const auto inner = std::max(0, static_cast<int>(rcPage.right) - pad * 2);

	const auto hdc = GetDC(m_hPage);
	const auto hPrevFont = SelectObject(hdc, *m_font);
	TEXTMETRICW tm{};
	GetTextMetricsW(hdc, &tm);
	const auto lineHeight = static_cast<int>(tm.tmHeight);
	const auto measure = [hdc](HWND hWnd, int width) {
		if (!hWnd || width <= 0)
			return 0;
		const auto text = GetText(hWnd);
		RECT rc{0, 0, width, 0};
		DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		return static_cast<int>(rc.bottom);
	};

	struct Placement {
		HWND Window;
		int X, Y, Width, Height;
	};
	std::vector<Placement> placements;
	const auto place = [&placements](HWND hWnd, int x, int y, int width, int height) {
		if (hWnd)
			placements.emplace_back(hWnd, x, y, width, height);
	};

	// Laid out with the grids at their own heights, then again with what the page has left shared among them alike.
	const auto layoutRows = [&](int gridExtra) {
		placements.clear();
		auto y = pad;
		auto flowGroup = 0;
		auto flowX = pad;
		auto lineGroup = 0;
		auto lineX = pad;
		for (const auto& row : m_rows) {
			if (row->TabOwner) {
				const auto shown = row->TabIndex == row->TabOwner->ActiveTab;
				for (const auto hWnd : {row->Label, row->Control, row->Button1, row->Button2, row->Button3, row->Button4, row->Description, row->Secondary}) {
					if (hWnd)
						ShowWindow(hWnd, shown ? SW_SHOWNA : SW_HIDE);
				}
				if (!shown)
					continue;
			}
			const auto previousFlowGroup = std::exchange(flowGroup, 0);
			const auto previousLineGroup = std::exchange(lineGroup, 0);
			SelectObject(hdc, *m_font);
			int rowHeight;
			if (row->LineGroup) {
				// After the row before it of its group, on its line if the label and a little of the control fit; the last
				// of the group (or of the line) takes the rest of the line.
				const auto next = &row + 1 != m_rows.data() + m_rows.size() ? (&row)[1].get() : nullptr;
				const auto labelText = GetText(row->Label);
				RECT rcLabel{};
				DrawTextW(hdc, labelText.c_str(), static_cast<int>(labelText.size()), &rcLabel, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
				const auto labelWidth = static_cast<int>(rcLabel.right);
				const auto wanted = scale(row->LineWidth ? row->LineWidth : 120);
				if (previousLineGroup == row->LineGroup && lineX + labelWidth + gap + wanted <= pad + inner)
					y -= controlHeight + rowGap;
				else
					lineX = pad;
				place(row->Label, lineX, y + std::max(0, (controlHeight - lineHeight) / 2), labelWidth, lineHeight);
				const auto controlX = lineX + labelWidth + gap;
				const auto rest = std::max(0, pad + inner - controlX);
				const auto last = !next || next->LineGroup != row->LineGroup || controlX + wanted + gap * 2 + scale(120) > pad + inner;
				const auto controlWidth = last ? rest : std::min(rest, wanted);
				// The height of a drop-down list is that of its list.
				place(row->Control, controlX, y, controlWidth, row->Type == Row::RowType::Edit ? controlHeight : scale(300));
				lineX = controlX + controlWidth + gap * 2;
				lineGroup = row->LineGroup;
				y += controlHeight + rowGap;
				continue;
			}
			switch (row->Type) {
				case Row::RowType::Heading: {
					y += scale(8);
					if (!row->Control) {
						rowHeight = measure(row->Label, inner);
						place(row->Label, pad, y, inner, rowHeight);
						break;
					}

					// The heading as wide as its text, in its bold font, and its check box after it on the same line.
					SelectObject(hdc, *m_boldFont);
					RECT rcHeading{};
					const auto heading = GetText(row->Label);
					DrawTextW(hdc, heading.c_str(), static_cast<int>(heading.size()), &rcHeading, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
					SelectObject(hdc, *m_font);
					RECT rcCheck{};
					const auto check = GetText(row->Control);
					DrawTextW(hdc, check.c_str(), static_cast<int>(check.size()), &rcCheck, DT_CALCRECT | DT_SINGLELINE);
					const auto headingWidth = std::min(inner, static_cast<int>(rcHeading.right) + scale(2));
					const auto checkX = pad + headingWidth + scale(16);
					const auto checkWidth2 = std::max(0, std::min(pad + inner - checkX, checkWidth + scale(4) + static_cast<int>(rcCheck.right) + scale(6)));
					rowHeight = std::max(lineHeight, static_cast<int>(rcHeading.bottom)) + 2;
					place(row->Label, pad, y + 1, headingWidth, rowHeight - 2);
					place(row->Control, checkX, y, checkWidth2, rowHeight);
					if (row->Button1) {
						RECT rcLink{};
						const auto link = GetText(row->Button1);
						DrawTextW(hdc, link.c_str(), static_cast<int>(link.size()), &rcLink, DT_CALCRECT | DT_SINGLELINE);
						const auto linkWidth = static_cast<int>(rcLink.right) + scale(6);
						place(row->Button1, std::max(checkX + checkWidth2 + gap, pad + inner - linkWidth), y, linkWidth, rowHeight);
					}
					break;
				}

				case Row::RowType::Note: {
					rowHeight = measure(row->Description, inner);
					place(row->Description, pad, y, inner, rowHeight);
					break;
				}

				case Row::RowType::Tabs: {
					// Side by side, as wide as each needs, going on to the next line where one doesn't fit.
					auto x = pad;
					auto lineY = y;
					for (const auto hButton : row->Tabs) {
						RECT rcText{};
						const auto text = GetText(hButton);
						DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_CALCRECT | DT_SINGLELINE);
						auto width = static_cast<int>(rcText.right) + scale(24);
						if (x > pad && x + width > pad + inner) {
							x = pad;
							lineY += controlHeight;
						}
						width = std::min(pad + inner - x, width);
						place(hButton, x, lineY, width, controlHeight);
						x += width;
					}
					rowHeight = lineY + controlHeight - y;
					break;
				}

				case Row::RowType::RichText: {
					// Three lines, with a button for the rest, if there is more; the control has a margin of its own.
					const auto full = measure(row->Description, std::max(1, inner - scale(8))) + scale(4);
					const auto collapsed = lineHeight * 3 + scale(4);
					const auto shortened = full > collapsed;
					const auto textHeight = shortened && !row->Expanded ? collapsed : full;
					place(row->Description, pad, y, inner, textHeight);
					rowHeight = textHeight;
					if (shortened) {
						RECT rcButton{};
						const auto buttonText = GetText(row->Button1);
						DrawTextW(hdc, buttonText.c_str(), static_cast<int>(buttonText.size()), &rcButton, DT_CALCRECT | DT_SINGLELINE);
						place(row->Button1, pad, y + textHeight + scale(4), std::max(buttonWidth, static_cast<int>(rcButton.right) + scale(24)), controlHeight);
						rowHeight += scale(4) + controlHeight;
					} else {
						place(row->Button1, 0, 0, 0, 0);
					}
					break;
				}

				case Row::RowType::Action: {
					// Each as wide as its text needs, side by side, going on to the next line where one doesn't fit.
					auto x = pad;
					auto lineY = y;
					for (const auto hButton : {row->Button1, row->Button2, row->Button3, row->Button4}) {
						if (!hButton)
							continue;
						RECT rcText{};
						const auto text = GetText(hButton);
						DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_CALCRECT | DT_SINGLELINE);
						auto width = std::max(buttonWidth, static_cast<int>(rcText.right) + scale(24));
						if (x > pad && x + width > pad + inner) {
							x = pad;
							lineY += controlHeight + gap;
						}
						width = std::min(pad + inner - x, width);
						place(hButton, x, lineY, width, controlHeight);
						x += width + gap;
					}
					rowHeight = lineY + controlHeight - y;
					break;
				}

				case Row::RowType::Grid: {
					// Its label and description above, the full width.
					auto gridY = y;
					if (const auto labelHeight = measure(row->Label, inner)) {
						place(row->Label, pad, gridY, inner, labelHeight);
						gridY += labelHeight + scale(2);
					}
					if (const auto descriptionHeight = measure(row->Description, inner)) {
						place(row->Description, pad, gridY, inner, descriptionHeight);
						gridY += descriptionHeight + scale(4);
					}
					const auto gridHeight = scale(row->Height ? row->Height : 200) + gridExtra;
					place(row->Control, pad, gridY, inner, gridHeight);
					rowHeight = gridY + gridHeight - y;
					break;
				}

				case Row::RowType::Preview: {
					// Its description above, the full width, at its own height.
					auto previewY = y;
					if (const auto descriptionHeight = measure(row->Description, inner)) {
						place(row->Description, pad, previewY, inner, descriptionHeight);
						previewY += descriptionHeight + scale(4);
					}
					const auto previewHeight = scale(row->Height);
					place(row->Control, pad, previewY, inner, previewHeight);
					rowHeight = previewY + previewHeight - y;
					break;
				}

				case Row::RowType::Check: {
					// The button has its label, and is as wide as it needs, wrapping it at the page's width; a line is
					// left above and below for the focus. A group's heading is spaced and measured as one.
					if (row->Bold) {
						y += scale(8);
						SelectObject(hdc, *m_boldFont);
					}
					const auto text = GetText(row->Control);
					// Where the button draws its label, so that the description lines up with it: after its glyph and a third
					// of the glyph's width, as the theme and CustomDrawDarkButton place it.
					const auto textX = [&] {
						if (const auto hTheme = OpenThemeData(row->Control, L"Button")) {
							const auto type = GetWindowLongPtrW(row->Control, GWL_STYLE) & BS_TYPEMASK;
							const auto part = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON ? BP_RADIOBUTTON : BP_CHECKBOX;
							SIZE glyph{};
							const auto measured = SUCCEEDED(GetThemePartSize(hTheme, hdc, part, 1, nullptr, TS_DRAW, &glyph));
							CloseThemeData(hTheme);
							if (measured && glyph.cx > 0)
								return static_cast<int>(glyph.cx + glyph.cx / 3);
						}
						return checkWidth + scale(4);
					}();
					RECT rcLine{};
					DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcLine, DT_CALCRECT | DT_SINGLELINE);
					const auto singleWidth = textX + static_cast<int>(rcLine.right) + scale(6);

					if (row->FlowGroup) {
						// After the one before it of its group, on its line if it fits, or else at the start of the next.
						if (previousFlowGroup == row->FlowGroup && flowX + singleWidth <= pad + inner)
							y -= lineHeight + 2 + rowGap;
						else
							flowX = pad;
						place(row->Control, flowX, y, std::min(singleWidth, pad + inner - flowX), lineHeight + 2);
						flowX += singleWidth + scale(14);
						flowGroup = row->FlowGroup;
						rowHeight = lineHeight + 2;
						break;
					}

					RECT rcText{0, 0, std::max(1, inner - textX - scale(6)), 0};
					DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);
					const auto textHeight = std::max(lineHeight, static_cast<int>(rcText.bottom)) + 2;
					const auto labelX = pad + textX;
					const auto labelWidth = inner - textX;
					const auto descriptionHeight = measure(row->Description, labelWidth);

					// A menu button at the line's right end, the check box beside it in the line's middle.
					auto controlWidth = std::min(inner, singleWidth);
					auto controlY = y;
					auto lineHeightUsed = textHeight;
					if (row->Button1) {
						RECT rcButton{};
						const auto buttonText = GetText(row->Button1);
						DrawTextW(hdc, buttonText.c_str(), static_cast<int>(buttonText.size()), &rcButton, DT_CALCRECT | DT_SINGLELINE);
						const auto menuWidth = std::max(scale(72), static_cast<int>(rcButton.right) + scale(24));
						place(row->Button1, pad + inner - menuWidth, y, menuWidth, controlHeight);
						controlWidth = std::min(controlWidth, std::max(0, inner - menuWidth - gap));
						controlY = y + std::max(0, (controlHeight - textHeight) / 2);
						lineHeightUsed = std::max(textHeight, controlHeight);
					}
					place(row->Control, pad, controlY, controlWidth, textHeight);
					place(row->Description, labelX, y + lineHeightUsed + scale(2), labelWidth, descriptionHeight);
					rowHeight = lineHeightUsed + (descriptionHeight ? scale(2) + descriptionHeight : 0);
					break;
				}

				case Row::RowType::PathList: {
					const auto labelHeight = measure(row->Label, inner);
					const auto descriptionHeight = measure(row->Description, inner);
					place(row->Label, pad, y, inner, labelHeight);
					auto listY = y + labelHeight + scale(4);
					if (descriptionHeight) {
						place(row->Description, pad, listY - scale(2), inner, descriptionHeight);
						listY += descriptionHeight + scale(2);
					}
					const auto listHeight = scale(96);
					place(row->Control, pad, listY, inner - buttonWidth - gap, listHeight);
					place(row->Button1, pad + inner - buttonWidth, listY, buttonWidth, controlHeight);
					place(row->Button2, pad + inner - buttonWidth, listY + controlHeight + scale(4), buttonWidth, controlHeight);
					rowHeight = listY + listHeight - y;
					break;
				}

				default: {
					// The label and its description on the left, the control on the right.
					const auto labelWidth = inner * 45 / 100;
					const auto controlX = pad + labelWidth + gap;
					const auto controlWidth = inner - labelWidth - gap;
					const auto labelY = y + std::max(0, (controlHeight - lineHeight) / 2);
					const auto labelHeight = measure(row->Label, labelWidth);
					const auto descriptionHeight = measure(row->Description, labelWidth);
					place(row->Label, pad, labelY, labelWidth, labelHeight);
					place(row->Description, pad, labelY + labelHeight + scale(2), labelWidth, descriptionHeight);
					switch (row->Type) {
						case Row::RowType::Edit:
							place(row->Control, controlX, y, controlWidth, controlHeight);
							break;
						case Row::RowType::Value:
							place(row->Control, controlX, labelY, controlWidth, lineHeight);
							break;
						case Row::RowType::Slider: {
							const auto numberWidth = scale(72);
							place(row->Control, controlX, y, controlWidth - numberWidth - gap, controlHeight);
							place(row->Secondary, controlX + controlWidth - numberWidth, y, numberWidth, controlHeight);
							break;
						}
						case Row::RowType::Combo:
						case Row::RowType::ComboEdit:
							// The height of a drop-down list is that of its list.
							place(row->Control, controlX, y, controlWidth, scale(300));
							break;
						case Row::RowType::PathEdit:
							place(row->Control, controlX, y, controlWidth - buttonWidth - gap, controlHeight);
							place(row->Button1, controlX + controlWidth - buttonWidth, y, buttonWidth, controlHeight);
							break;
						case Row::RowType::Unsupported:
							place(row->Button1, controlX, y, std::min(controlWidth, buttonWidth * 3 / 2), controlHeight);
							break;
						default:
							break;
					}
					rowHeight = std::max(controlHeight, labelY - y + labelHeight + (descriptionHeight ? scale(2) + descriptionHeight : 0));
					break;
				}
			}
			y += rowHeight + rowGap;
		}
		return y;
	};
	auto y = layoutRows(0);
	if (const auto grids = static_cast<int>(std::ranges::count_if(m_rows, [](const auto& row) { return row->Type == Row::RowType::Grid && (!row->TabOwner || row->TabIndex == row->TabOwner->ActiveTab); }))) {
		if (const auto spare = static_cast<int>(rcPage.bottom) - (y - rowGap + pad); spare > 0)
			y = layoutRows(spare / grids);
	}
	SelectObject(hdc, hPrevFont);
	ReleaseDC(m_hPage, hdc);

	m_contentHeight = m_rows.empty() ? 0 : y - rowGap + pad;
	m_scrollY = std::clamp(m_scrollY, 0, std::max(0, m_contentHeight - static_cast<int>(rcPage.bottom)));

	auto hdwp = BeginDeferWindowPos(static_cast<int>(placements.size()));
	for (const auto& p : placements) {
		if (hdwp)
			hdwp = DeferWindowPos(hdwp, p.Window, nullptr, p.X, p.Y - m_scrollY, p.Width, p.Height, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (hdwp)
		EndDeferWindowPos(hdwp);

	SCROLLINFO si{
		.cbSize = sizeof(SCROLLINFO),
		.fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
		.nMin = 0,
		.nMax = std::max(0, m_contentHeight - 1),
		.nPage = static_cast<UINT>(rcPage.bottom),
		.nPos = m_scrollY,
	};
	SetScrollInfo(m_hPage, SB_VERT, &si, TRUE);
	InvalidateRect(m_hPage, nullptr, TRUE);

	// The font preview is drawn at its size: anew once that changes, as the page is resized.
	if (m_fontPreview) {
		RECT rcPreview;
		GetClientRect(m_fontPreview->Control, &rcPreview);
		if (rcPreview.right != m_fontPreview->Requested.cx || rcPreview.bottom != m_fontPreview->Requested.cy)
			RequestFontPreview(FontPreviewDelay);
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ScrollPageTo(int y) {
	RECT rcPage;
	GetClientRect(m_hPage, &rcPage);
	y = std::clamp(y, 0, std::max(0, m_contentHeight - static_cast<int>(rcPage.bottom)));
	if (y == m_scrollY)
		return;
	m_scrollY = y;
	LayoutPage();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ScrollIntoView(HWND hControl) {
	RECT rcPage, rcControl;
	GetClientRect(m_hPage, &rcPage);
	GetWindowRect(hControl, &rcControl);
	MapWindowPoints(nullptr, m_hPage, reinterpret_cast<POINT*>(&rcControl), 2);
	const auto margin = static_cast<int>(12 * GetZoom());
	if (rcControl.top < margin)
		ScrollPageTo(m_scrollY + rcControl.top - margin);
	else if (rcControl.bottom > rcPage.bottom - margin)
		ScrollPageTo(m_scrollY + rcControl.bottom - rcPage.bottom + margin);
}

LRESULT XivAlexander::Apps::MainApp::Window::SettingsView::PageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_COMMAND:
			OnPageCommand(LOWORD(wParam), HIWORD(wParam));
			return 0;

		case WM_DRAWITEM: {
			// A link: underlined text in the theme's link color, and the focus around it.
			const auto& dis = *reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
			const auto isLink = std::ranges::any_of(m_rows, [&dis](const auto& row) { return row->LinkButton && row->Button1 == dis.hwndItem; });
			if (!isLink)
				break;
			FillRect(dis.hDC, &dis.rcItem, *m_backgroundBrush);
			LOGFONTW logFont{};
			GetObjectW(*m_font, sizeof logFont, &logFont);
			logFont.lfUnderline = TRUE;
			const auto hUnderline = CreateFontIndirectW(&logFont);
			const auto hPrevFont = SelectObject(dis.hDC, hUnderline);
			SetBkMode(dis.hDC, TRANSPARENT);
			SetTextColor(dis.hDC, (dis.itemState & ODS_DISABLED) ? GetThemeColors(IsDarkModeEnabled()).ForegroundWeak : IsDarkModeEnabled() ? RGB(0x66, 0xB3, 0xFF) : RGB(0x00, 0x66, 0xCC));
			const auto text = GetText(dis.hwndItem);
			RECT rc = dis.rcItem;
			DrawTextW(dis.hDC, text.c_str(), static_cast<int>(text.size()), &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
			if ((dis.itemState & ODS_FOCUS) && !(dis.itemState & ODS_NOFOCUSRECT))
				DrawFocusRect(dis.hDC, &dis.rcItem);
			DeleteObject(SelectObject(dis.hDC, hPrevFont));
			return TRUE;
		}

		case WM_SETCURSOR:
			if (std::ranges::any_of(m_rows, [wParam](const auto& row) { return row->LinkButton && row->Button1 == reinterpret_cast<HWND>(wParam); })) {
				SetCursor(LoadCursorW(nullptr, IDC_HAND));
				return TRUE;
			}
			break;

		case WM_CONTEXTMENU:
			for (const auto& row : m_rows) {
				if (row->ContextMenu && (row->Control == reinterpret_cast<HWND>(wParam) || row->Label == reinterpret_cast<HWND>(wParam))) {
					POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					if (pt.x == -1 && pt.y == -1) {
						// From the keyboard: below the control.
						RECT rc{};
						GetWindowRect(row->Control, &rc);
						pt = {rc.left, rc.bottom};
					}
					row->ContextMenu(pt);
					return 0;
				}
			}
			break;

		case WM_NOTIFY:
			if (reinterpret_cast<LPNMHDR>(lParam)->code == EN_LINK) {
				const auto& link = *reinterpret_cast<ENLINK*>(lParam);
				if (link.msg != WM_LBUTTONUP)
					return 0;
				std::wstring url(static_cast<size_t>(std::max(0L, link.chrg.cpMax - link.chrg.cpMin)) + 1, L'\0');
				TEXTRANGEW range{.chrg = link.chrg, .lpstrText = url.data()};
				url.resize(static_cast<size_t>(SendMessageW(link.nmhdr.hwndFrom, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range))));
				try {
					Utils::Win32::ShellExecutePathOrThrow(url, m_hWndOwner);
				} catch (const std::exception& e) {
					Dll::MessageBoxF(m_hWndOwner, MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
				}
				return 1;
			}
			// The dark theme draws the text of radio buttons as if they were disabled; these draw it themselves.
			if (IsDarkModeEnabled() && reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW) {
				if (const auto result = CustomDrawDarkButton(*reinterpret_cast<LPNMCUSTOMDRAW>(lParam)))
					return *result;
			}
			break;

		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			if (const auto res = OnControlColor(uMsg, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)))
				return *res;
			break;

		case WM_ERASEBKGND: {
			RECT rc;
			GetClientRect(hwnd, &rc);
			FillRect(reinterpret_cast<HDC>(wParam), &rc, *m_backgroundBrush);
			return 1;
		}

		case WM_SIZE:
			LayoutPage();
			return 0;

		case WM_LBUTTONDOWN:
			// The background takes the focus, so that a click on it ends what was being edited; dragging it moves the
			// window, as its title bar would.
			SetFocus(hwnd);
			DragWindow();
			return 0;

		case WM_HSCROLL:
			// A slider moved.
			for (const auto& row : m_rows) {
				if (row->Type == Row::RowType::Slider && row->Control == reinterpret_cast<HWND>(lParam) && row->Click)
					row->Click(PartControl);
			}
			return 0;

		case WM_VSCROLL: {
			SCROLLINFO si{.cbSize = sizeof(SCROLLINFO), .fMask = SIF_ALL};
			GetScrollInfo(hwnd, SB_VERT, &si);
			const auto line = static_cast<int>(24 * GetZoom());
			auto y = m_scrollY;
			switch (LOWORD(wParam)) {
				case SB_LINEUP: y -= line; break;
				case SB_LINEDOWN: y += line; break;
				case SB_PAGEUP: y -= static_cast<int>(si.nPage); break;
				case SB_PAGEDOWN: y += static_cast<int>(si.nPage); break;
				case SB_THUMBTRACK:
				case SB_THUMBPOSITION: y = si.nTrackPos; break;
				case SB_TOP: y = 0; break;
				case SB_BOTTOM: y = m_contentHeight; break;
			}
			ScrollPageTo(y);
			return 0;
		}

		case WM_MOUSEWHEEL:
			ScrollPageTo(m_scrollY - GET_WHEEL_DELTA_WPARAM(wParam) * static_cast<int>(72 * GetZoom()) / WHEEL_DELTA);
			return 0;
	}
	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::OnPageCommand(int controlId, int notification) {
	if (controlId >= FirstTabControlId && controlId < FirstRowControlId) {
		if (notification != BN_CLICKED)
			return;
		for (const auto& row : m_rows) {
			if (row->Type == Row::RowType::Tabs && row->Click && static_cast<size_t>(controlId - FirstTabControlId) < row->Tabs.size())
				row->Click(controlId - FirstTabControlId);
		}
		return;
	}
	if (controlId < FirstRowControlId)
		return;
	const auto index = static_cast<size_t>((controlId - FirstRowControlId) / ControlsPerRow);
	const auto part = (controlId - FirstRowControlId) % ControlsPerRow;
	if (index >= m_rows.size())
		return;

	auto& row = *m_rows[index];
	const auto click = [&row](int clickedPart) {
		if (row.Click)
			row.Click(clickedPart);
	};

	// The notification codes of different kinds of controls overlap, so they are told apart by what was notified.
	if (part == PartLabel) {
		if (notification == STN_CLICKED && row.Type == Row::RowType::Check)
			click(PartLabel);
		return;
	}

	if (part == PartButton1 || part == PartButton2 || part == PartButton3 || part == PartButton4) {
		if (notification == BN_CLICKED)
			click(part);
		else if (notification == BN_SETFOCUS)
			ScrollIntoView(part == PartButton1 ? row.Button1 : part == PartButton2 ? row.Button2 : part == PartButton3 ? row.Button3 : row.Button4);
		return;
	}

	if (part == PartSecondary) {
		if (notification == EN_KILLFOCUS && row.Commit)
			row.Commit();
		else if (notification == EN_SETFOCUS)
			ScrollIntoView(row.Secondary);
		return;
	}

	if (part != PartControl)
		return;

	switch (row.Type) {
		case Row::RowType::Check:
		case Row::RowType::Heading:
			if (notification == BN_CLICKED)
				click(PartControl);
			else if (notification == BN_SETFOCUS)
				ScrollIntoView(row.Control);
			break;

		case Row::RowType::Edit:
		case Row::RowType::PathEdit:
			if ((notification == EN_KILLFOCUS || (notification == EN_CHANGE && row.CommitOnChange)) && row.Commit)
				row.Commit();
			else if (notification == EN_SETFOCUS)
				ScrollIntoView(row.Control);
			break;

		case Row::RowType::Combo:
			if (notification == CBN_SELCHANGE && row.Commit)
				row.Commit();
			else if (notification == CBN_SETFOCUS)
				ScrollIntoView(row.Control);
			break;

		case Row::RowType::ComboEdit:
			if (notification == CBN_SELCHANGE)
				click(PartControl);
			else if ((notification == CBN_KILLFOCUS || (notification == CBN_EDITCHANGE && row.CommitOnChange)) && row.Commit)
				row.Commit();
			else if (notification == CBN_SETFOCUS)
				ScrollIntoView(row.Control);
			break;

		case Row::RowType::PathList:
			if (notification == LBN_DBLCLK)
				click(PartControl);
			else if (notification == LBN_SETFOCUS)
				ScrollIntoView(row.Control);
			break;

		default:
			break;
	}
}

std::optional<LRESULT> XivAlexander::Apps::MainApp::Window::SettingsView::OnControlColor(UINT uMsg, HDC hdc, HWND hControl) {
	const auto dark = IsDarkModeEnabled();
	const auto& colors = GetThemeColors(dark);
	switch (uMsg) {
		case WM_CTLCOLORSTATIC: {
			const auto weak = std::ranges::any_of(m_rows, [hControl](const auto& row) { return row->Description == hControl; });
			SetTextColor(hdc, weak ? colors.ForegroundWeak : colors.GetForeground());
			SetBkColor(hdc, colors.GetBackground());
			return reinterpret_cast<LRESULT>(*m_backgroundBrush);
		}

		case WM_CTLCOLORBTN:
			SetBkColor(hdc, colors.GetBackground());
			return reinterpret_cast<LRESULT>(*m_backgroundBrush);

		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			if (!dark)
				return std::nullopt;
			SetTextColor(hdc, colors.GetForeground());
			SetBkColor(hdc, colors.GetBackground());
			return reinterpret_cast<LRESULT>(*m_backgroundBrush);
	}
	return std::nullopt;
}

LRESULT XivAlexander::Apps::MainApp::Window::SettingsView::ContainerProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_ERASEBKGND: {
			// The divider; the tree and the page or the status cover the rest.
			RECT rc;
			GetClientRect(hwnd, &rc);
			FillRect(reinterpret_cast<HDC>(wParam), &rc, *m_backgroundBrush);
			const auto rcGrab = GetDividerRect();
			const auto lineX = (rcGrab.left + rcGrab.right - DividerSize) / 2;
			RECT rcDivider{lineX, rc.top, lineX + DividerSize, rc.bottom};
			FillRect(reinterpret_cast<HDC>(wParam), &rcDivider, *GetThemeColors(IsDarkModeEnabled()).CreateBackgroundWeakBrush());
			return 1;
		}

		case WM_SIZE:
			Layout();
			return 0;

		case WM_SETCURSOR:
			// Only the divider is of this window itself; the tree and the page cover the rest.
			if (reinterpret_cast<HWND>(wParam) == hwnd && LOWORD(lParam) == HTCLIENT) {
				SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
				return TRUE;
			}
			break;

		case WM_LBUTTONDOWN:
			m_dividerDragOffset = GET_X_LPARAM(lParam) - GetDividerRect().left;
			SetCapture(hwnd);
			return 0;

		case WM_MOUSEMOVE:
			if (m_dividerDragOffset) {
				m_draggedTreeWidth = static_cast<int>(std::lround((GET_X_LPARAM(lParam) - *m_dividerDragOffset) / GetZoom()));
				Layout();
				InvalidateRect(hwnd, nullptr, TRUE);
			}
			return 0;

		case WM_LBUTTONUP:
			if (m_dividerDragOffset)
				ReleaseCapture();
			return 0;

		case WM_CAPTURECHANGED:
			m_dividerDragOffset.reset();
			if (m_draggedTreeWidth) {
				// Where it is shown, within the bounds of the window.
				m_config->Runtime.Ui.MainWindow.SettingsTreeWidth = static_cast<int>(std::lround(GetDividerRect().left / GetZoom()));
				m_draggedTreeWidth.reset();
			}
			return 0;

		case WM_NOTIFY: {
			const auto nmhdr = reinterpret_cast<LPNMHDR>(lParam);
			if (nmhdr->hwndFrom == m_hTree && nmhdr->code == NM_CUSTOMDRAW) {
				// The folders, ModPacks and font families turned off are struck through.
				const auto& nmcd = reinterpret_cast<LPNMTVCUSTOMDRAW>(lParam)->nmcd;
				if (nmcd.dwDrawStage == CDDS_PREPAINT)
					return CDRF_NOTIFYITEMDRAW;
				if (nmcd.dwDrawStage == CDDS_ITEMPREPAINT && nmcd.lItemlParam) {
					const auto& node = *reinterpret_cast<const TreeNode*>(nmcd.lItemlParam);
					const auto isFamilyDisabled = [this, &node] {
						const auto& families = m_config->Runtime.FontReplacement.Faces.Families.Value();
						const auto it = families.find(node.Family);
						return it != families.end() && !it->second.Enabled;
					};
					if (((node.Kind == TreeNode::NodeKind::TtmpFolder || node.Kind == TreeNode::NodeKind::TtmpPack) && m_ttmpDisabled.contains(node.TtmpPath))
						|| (node.Kind == TreeNode::NodeKind::FontFamily && isFamilyDisabled())) {
						SelectObject(nmcd.hdc, *m_strikeFont);
						return CDRF_NEWFONT;
					}
				}
				return CDRF_DODEFAULT;
			}
			if (nmhdr->hwndFrom == m_hTree && nmhdr->code == TVN_KEYDOWN && reinterpret_cast<LPNMTVKEYDOWN>(lParam)->wVKey == VK_F2) {
				RenameShownTtmp();
				return 0;
			}
			if (nmhdr->hwndFrom == m_hTree && nmhdr->code == TVN_BEGINLABELEDITW) {
				// Only the folders and the ModPacks, which are directories; true refuses.
				const auto pNode = reinterpret_cast<const TreeNode*>(reinterpret_cast<LPNMTVDISPINFOW>(lParam)->item.lParam);
				const auto refuse = !pNode || !((pNode->Kind == TreeNode::NodeKind::TtmpFolder && !pNode->TtmpPath.empty()) || pNode->Kind == TreeNode::NodeKind::TtmpPack);
				if (!refuse) {
					if (const auto hEdit = TreeView_GetEditControl(m_hTree))
						SetWindowSubclass(hEdit, LabelEditSubclassProc, LabelEditSubclassId, 0);
				}
				return refuse;
			}
			if (nmhdr->hwndFrom == m_hTree && nmhdr->code == TVN_ENDLABELEDITW) {
				const auto& item = reinterpret_cast<LPNMTVDISPINFOW>(lParam)->item;
				const auto pNode = reinterpret_cast<const TreeNode*>(item.lParam);
				if (pNode && item.pszText) {
					// The tree is built anew from the library once it changes, so the label is left as it was.
					const auto path = pNode->TtmpPath;
					const auto name = Trim(item.pszText);
					if (const auto target = FindTtmp(path); target && !name.empty() && name != path.filename().wstring()) {
						auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
						RunTtmpOperation([&] { sqpacks->RenameTtmp(target, name); }, path.parent_path() / name);
					}
				}
				return FALSE;
			}
			if (nmhdr->hwndFrom == m_hTree && nmhdr->code == TVN_SELCHANGEDW) {
				if (!m_populatingTree) {
					// The tree may take the focus only after this, when the page being edited is gone.
					CommitTypedText();
					ShowPage(reinterpret_cast<const TreeNode*>(reinterpret_cast<LPNMTREEVIEWW>(nmhdr)->itemNew.lParam));
				}
				return 0;
			}
			break;
		}

		case WmRefreshRows:
			RefreshRows();
			return 0;

		case WmPopulateTree:
			PopulateTree();
			return 0;

		case WmRefreshTheme:
			RefreshTheme();
			return 0;

		case WmListenToCooldowns:
			m_cooldownListener.clear();
			if (auto& handler = m_app.GetNetworkTimingHandler()) {
				m_cooldownListener = handler->OnCooldownGroupUpdateListener([hWnd = m_hWnd](const Features::NetworkTimingHandler::CooldownGroup& group, bool newDriftItem) {
					if (group.Id == Features::NetworkTimingHandler::CooldownGroup::Id_Gcd && newDriftItem)
						PostMessageW(hWnd, WmCooldown, static_cast<WPARAM>(group.DurationUs), static_cast<LPARAM>(group.DriftTrackerUs.Latest()));
				});
			}
			return 0;

		case WmCooldown:
			m_cooldownHistory.emplace_front(static_cast<uint64_t>(wParam), static_cast<int64_t>(lParam));
			if (m_cooldownHistory.size() > 1024)
				m_cooldownHistory.pop_back();
			RefreshRows();
			return 0;

		case WmRebuildPage:
			ShowPage(m_pNode);
			return 0;

		case WmTtmpSetsChanged:
			OnTtmpSetsChanged();
			return 0;

		case WmSelectTtmpPack:
			SelectNode([this](const TreeNode& node) {
				return (node.Kind == TreeNode::NodeKind::TtmpPack || node.Kind == TreeNode::NodeKind::TtmpFolder) && !node.TtmpPath.empty() && node.TtmpPath == m_ttmpPackToSelect;
			});
			if (std::exchange(m_ttmpRenameAfterSelect, false))
				RenameShownTtmp();
			return 0;

		case WmFontPreviewDrawn:
			OnFontPreviewDrawn();
			return 0;

		case WM_DESTROY:
			// Nothing is shown any more; the object goes with its host.
			m_cooldownListener.clear();
			m_rowCleanup.clear();
			m_cleanup.clear();
			m_fontPreview.reset();
			m_rows.clear();
			break;
	}
	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT XivAlexander::Apps::MainApp::Window::SettingsView::StatusProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_ERASEBKGND:
			return 1;

		case WM_LBUTTONDOWN:
			DragWindow();
			return 0;

		case WM_PAINT: {
			// Drawn by the host, off screen first.
			PAINTSTRUCT ps{};
			const auto hdc = BeginPaint(hwnd, &ps);
			RECT rc;
			GetClientRect(hwnd, &rc);
			const auto hdcBack = CreateCompatibleDC(hdc);
			const auto hBitmap = CreateCompatibleBitmap(hdc, std::max(1L, rc.right), std::max(1L, rc.bottom));
			const auto hPrevBitmap = SelectObject(hdcBack, hBitmap);
			FillRect(hdcBack, &rc, *m_backgroundBrush);
			if (m_paintStatus)
				m_paintStatus(hdcBack, rc);
			BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcBack, 0, 0, SRCCOPY);
			SelectObject(hdcBack, hPrevBitmap);
			DeleteObject(hBitmap);
			DeleteDC(hdcBack);
			EndPaint(hwnd, &ps);
			return 0;
		}
	}
	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::Layout() {
	RECT rc;
	GetClientRect(m_hWnd, &rc);
	const auto zoom = GetZoom();
	UpdateFont(zoom);
	const auto rcDivider = GetDividerRect();
	const auto treeWidth = static_cast<int>(rcDivider.left);
	const auto pageX = static_cast<int>(rcDivider.right);
	const auto pageWidth = std::max(0, static_cast<int>(rc.right) - pageX);
	SetWindowPos(m_hTree, nullptr, 0, 0, treeWidth, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(m_hPage, nullptr, pageX, 0, pageWidth, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(m_hStatus, nullptr, pageX, 0, pageWidth, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);

}

void XivAlexander::Apps::MainApp::Window::SettingsView::DragWindow() const {
	// The title bar's move, which waits for the mouse to move before moving anything.
	ReleaseCapture();
	SendMessageW(GetAncestor(m_hWnd, GA_ROOT), WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

RECT XivAlexander::Apps::MainApp::Window::SettingsView::GetDividerRect() const {
	// Where the tree ends, leaving either pane some room however small the window is.
	RECT rc;
	GetClientRect(m_hWnd, &rc);
	const auto zoom = GetZoom();
	const auto grab = std::max(DividerSize, static_cast<int>(DividerGrabSize * zoom));
	const auto minimum = static_cast<int>(80 * zoom);
	const auto maximum = std::max(minimum, static_cast<int>(rc.right) - grab - static_cast<int>(160 * zoom));
	const auto treeWidth = std::clamp(static_cast<int>(m_draggedTreeWidth.value_or(m_config->Runtime.Ui.MainWindow.SettingsTreeWidth.Value()) * zoom), minimum, maximum);
	return {treeWidth, rc.top, treeWidth + grab, rc.bottom};
}

void XivAlexander::Apps::MainApp::Window::SettingsView::UpdateFont(double zoom) {
	if (zoom == m_fontZoom && m_font)
		return;
	m_fontZoom = zoom;

	NONCLIENTMETRICSW ncm{.cbSize = sizeof(NONCLIENTMETRICSW)};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	// The metrics are at the system DPI; the window may be on a monitor of another.
	const auto hdcScreen = GetDC(nullptr);
	const auto systemDpi = GetDeviceCaps(hdcScreen, LOGPIXELSY);
	ReleaseDC(nullptr, hdcScreen);
	ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, static_cast<int>(zoom * 96), systemDpi);

	// The previous fonts are deleted once no control uses them.
	auto font = Font(CreateFontIndirectW(&ncm.lfMessageFont), true);
	auto strikeLogFont = ncm.lfMessageFont;
	strikeLogFont.lfStrikeOut = TRUE;
	auto strikeFont = Font(CreateFontIndirectW(&strikeLogFont), true);
	ncm.lfMessageFont.lfWeight = FW_BOLD;
	auto boldFont = Font(CreateFontIndirectW(&ncm.lfMessageFont), true);
	SendMessageW(m_hTree, WM_SETFONT, reinterpret_cast<WPARAM>(*font), TRUE);
	EnumChildWindows(m_hPage, [](HWND hChild, LPARAM lParam) {
		SendMessageW(hChild, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
		return TRUE;
	}, reinterpret_cast<LPARAM>(*font));
	for (const auto& row : m_rows) {
		if (row->Type == Row::RowType::Heading)
			SendMessageW(row->Label, WM_SETFONT, reinterpret_cast<WPARAM>(*boldFont), TRUE);
		else if (row->Bold)
			SendMessageW(row->Control, WM_SETFONT, reinterpret_cast<WPARAM>(*boldFont), TRUE);
		else if (row->Grid)
			row->Grid->SetZoom(zoom, *font);
	}
	m_font = std::move(font);
	m_boldFont = std::move(boldFont);
	m_strikeFont = std::move(strikeFont);
	LayoutPage();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ApplyThemeToControl(HWND hControl) const {
	ApplyDarkModeToControl(hControl, IsDarkModeEnabled());
}

void XivAlexander::Apps::MainApp::Window::SettingsView::RefreshTheme() {
	const auto dark = IsDarkModeEnabled();
	const auto& colors = GetThemeColors(dark);
	m_backgroundBrush = colors.CreateBackgroundBrush();

	ApplyThemeToControl(m_hTree);
	TreeView_SetBkColor(m_hTree, dark ? colors.GetBackground() : static_cast<COLORREF>(-1));
	TreeView_SetTextColor(m_hTree, dark ? colors.GetForeground() : static_cast<COLORREF>(-1));

	ApplyThemeToControl(m_hPage);
	EnumChildWindows(m_hPage, [](HWND hChild, LPARAM lParam) {
		reinterpret_cast<const SettingsView*>(lParam)->ApplyThemeToControl(hChild);
		return TRUE;
	}, reinterpret_cast<LPARAM>(this));
	for (const auto& row : m_rows) {
		if (row->Grid)
			row->Grid->ApplyTheme(dark);
	}
	// The preview's image has colors of its own (FontPreviewColorPresets); its frame is the theme's, drawn with it.
	RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}
