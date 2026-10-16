#include "pch.h"
#include "MainApp/Windows/SettingsView.h"
#include "MainApp/Windows/ConfigWindow.h"
#include "MainApp/Windows/ThemeColors.h"

#include "Utils/StringUtils.h"
#include "Utils/Win32/Resource.h"
#include "Utils/Win32/TaskDialogBuilder.h"

#include "Config.h"
#include "MainApp/App.h"
#include "MainApp/Features/LoginSessions.h"
#include "MainApp/Features/AudioResampler.h"
#include "MainApp/Features/CrowdFix.h"
#include "MainApp/Features/NetworkTimingHandler.h"
#include <FontChanger.FixedSizeFont/image_glyph_files.h>

#include "MainApp/FontReplacement/GameFontNames.h"
#include "MainApp/FontReplacement/Presets.h"
#include "MainApp/FontReplacement/SubpixelText.h"
#include "MainApp/Modding/NestedTtmp.h"
#include "MainApp/Modding/ResourceOverrider.h"
#include "MainApp/Modding/VirtualSqPacks.h"
#include "Misc/GameInstallationDetector.h"
#include "resource.h"
#include "XivAlexander.h"

namespace {
	constexpr auto PageClassName = L"XivAlexander::Window::SettingsView::Page";

	// The tree/page divider, matching the line under the host's menu bar, centered in its wider grab area.
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
		WmListenToCrowdFix,
		WmCrowdFixStatusChanged,
	};

	// The controls of the n-th row of the page have the IDs FirstRowControlId + n * ControlsPerRow + RowPart.
	constexpr int FirstRowControlId = 1000;
	constexpr int FirstTabControlId = 900;  // Of the buttons of the page's tabs, below its rows'.
	constexpr int ControlsPerRow = 10;

	enum RowPart : int {
		PartLabel,
		PartControl,
		PartButton1,
		PartButton2,
		PartDescription,
		PartSecondary,
		PartButton3,
		PartButton4,
		PartHelp,
		PartStatus,
	};

	// The tool of the tooltip shown while a "(?)" is clicked or focused; the others are the buttons, by their handles.
	constexpr UINT_PTR TrackedHelpToolId = 1;

	constexpr auto HelpMark = L"(?)";

	constexpr auto ContainerClassName = L"XivAlexander::Window::SettingsView";
	constexpr auto StatusClassName = L"XivAlexander::Window::SettingsView::Status";

	using Utils::Win32::GetWindowString;

	std::wstring Trim(std::wstring s) {
		const auto first = s.find_first_not_of(L" \t");
		if (first == std::wstring::npos)
			return {};
		return s.substr(first, s.find_last_not_of(L" \t") - first + 1);
	}

	// An item without text is a separator.
	struct PopupMenuItem {
		std::wstring Text;
		std::function<void()> Action;
		bool Enabled = true;
		bool Default = false;
		bool Checked = false;
	};

	void ShowPopupMenu(HWND hWnd, POINT ptScreen, const std::vector<PopupMenuItem>& items, UINT flags = TPM_RIGHTBUTTON) {
		const auto hMenu = CreatePopupMenu();
		const auto destroyMenu = xivres::util::on_dtor([hMenu] { DestroyMenu(hMenu); });
		for (size_t i = 0; i < items.size(); ++i) {
			if (items[i].Text.empty()) {
				AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
				continue;
			}
			AppendMenuW(hMenu, MF_STRING | (items[i].Enabled ? 0 : MF_GRAYED) | (items[i].Checked ? MF_CHECKED : 0), i + 1, items[i].Text.c_str());
			if (items[i].Default)
				SetMenuDefaultItem(hMenu, static_cast<UINT>(i + 1), FALSE);
		}
		const auto chosen = TrackPopupMenu(hMenu, TPM_RETURNCMD | flags, ptScreen.x, ptScreen.y, 0, hWnd, nullptr);
		if (chosen > 0 && static_cast<size_t>(chosen) <= items.size() && items[chosen - 1].Action)
			items[chosen - 1].Action();
	}

	/// Drops the shortcut after the tab and the access key, whether marked in place ("&Enable") or after the text ("有効(&E)").
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

	/// Per-purpose client GUID so each purpose's dialogs remember their own last folder; FNV-1a of the name (two seeds), stable across builds.
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
			// RFC 9562 version 8 (custom) and variant bits, so it can't be taken for another kind of GUID.
			.Data3 = static_cast<uint16_t>((high & 0x0FFF) | 0x8000),
		};
		for (size_t i = 0; i < 8; ++i)
			guid.Data4[i] = static_cast<uint8_t>(low >> (56 - i * 8));
		guid.Data4[0] = static_cast<uint8_t>((guid.Data4[0] & 0x3F) | 0x80);
		return guid;
	}

	enum class PickKind {
		Folder,
		Dll,
		Json,
		Preset,
	};

	/// purpose keys the dialog's remembered last folder (DialogGuid).
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
			} else if (kind == PickKind::Preset || kind == PickKind::Json) {
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

	/// With the file type's "edit" verb; .json usually has none, so then with what opens it (or "Open with").
	void EditFile(HWND hOwner, const std::filesystem::path& path) {
		std::error_code ec;
		if (!is_regular_file(path, ec)) {
			MessageBeep(MB_ICONWARNING);
			return;
		}
		for (const auto verb : {L"edit", static_cast<const wchar_t*>(nullptr)}) {
			SHELLEXECUTEINFOW shex{
				.cbSize = sizeof shex,
				.fMask = verb ? static_cast<ULONG>(SEE_MASK_FLAG_NO_UI) : 0,
				.hwnd = hOwner,
				.lpVerb = verb,
				.lpFile = path.c_str(),
				.nShow = SW_SHOW,
			};
			if (ShellExecuteExW(&shex))
				return;
			const auto err = GetLastError();
			if (err == ERROR_CANCELLED)
				return;
			if (verb)
				continue;
			Dll::MessageBoxF(hOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, Utils::Win32::Error(err, "ShellExecuteExW").what());
			return;
		}
	}

	template<typename T>
	std::wstring FormatNumber(T value) {
		if constexpr (std::is_floating_point_v<T>)
			return std::format(L"{:g}", value);
		else
			return std::format(L"{}", value);
	}

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

	template<typename T, typename TFn>
	void EditAt(XivAlexander::ConfigItem<std::vector<T>>& item, size_t index, TFn&& fn) {
		auto values = item.Value();
		if (index >= values.size())
			return;
		fn(values[index]);
		item = std::move(values);
	}

	/// Moves to before the element currently at index to, as GridView::Source::MoveRow.
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

	using ActionButtons = std::vector<std::pair<std::wstring, std::function<void()>>>;

	template<typename T>
	ActionButtons AddRemoveButtons(const XivAlexander::RuntimeConfigRepository& rt, XivAlexander::ConfigItem<std::vector<T>>& item, XivAlexander::Apps::MainApp::Window::GridView& grid, T value, size_t column) {
		return {
			{rt.GetStringRes(IDS_SETTINGS_GRID_ADD), AddTo(item, grid, std::move(value), column)},
			{rt.GetStringRes(IDS_SETTINGS_REMOVE), RemoveFrom(item, grid)},
		};
	}

	using ToggledPathList = XivAlexander::ConfigItem<std::vector<XivAlexander::ToggledPath>>;

	std::function<bool(size_t, size_t)> GetToggledPathEnabled(ToggledPathList& item) {
		return [&item](size_t row, size_t) { return row < item.Value().size() && item.Value()[row].Enabled; };
	}

	std::function<void(size_t, size_t, bool)> SetToggledPathEnabled(ToggledPathList& item) {
		return [&item](size_t row, size_t, bool checked) {
			EditAt(item, row, [&](XivAlexander::ToggledPath& entry) { entry.Enabled = checked; });
		};
	}

	std::function<bool(size_t, size_t, const std::wstring&)> SetToggledPath(ToggledPathList& item) {
		return [&item](size_t row, size_t, const std::wstring& text) {
			auto value = Trim(text);
			if (value.empty())
				return false;
			EditAt(item, row, [&](XivAlexander::ToggledPath& entry) { entry.Path = std::move(value); });
			return true;
		};
	}

	std::function<void(size_t)> OpenRowInExplorer(HWND hOwner, ToggledPathList& item, std::function<std::filesystem::path(size_t)> resolve) {
		return [hOwner, &item, resolve = std::move(resolve)](size_t row) {
			if (row < item.Value().size())
				OpenInExplorer(hOwner, resolve(row));
		};
	}

	/// Add (picked with the dialog; purpose as PickPath's), Remove, and Show in Explorer.
	ActionButtons ToggledPathButtons(const XivAlexander::RuntimeConfigRepository& rt, HWND hOwner, ToggledPathList& item, XivAlexander::Apps::MainApp::Window::GridView& grid,
		UINT addLabelId, PickKind kind, std::string purpose, std::function<void(size_t)> openRow) {
		return {
			{rt.GetStringRes(addLabelId), [hOwner, &item, &grid, kind, purpose = std::move(purpose)] {
				if (const auto path = PickPath(hOwner, {}, kind, purpose)) {
					auto values = item.Value();
					values.push_back({.Path = *path});
					item = std::move(values);
					grid.Refresh();
					grid.Select(item.Value().size() - 1);
				}
			}},
			{rt.GetStringRes(IDS_SETTINGS_REMOVE), RemoveFrom(item, grid)},
			{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [&grid, openRow = std::move(openRow)] {
				if (const auto row = grid.GetSelectedRow())
					openRow(*row);
			}},
		};
	}

	// DirectWrite's weight, stretch, and style values, with their names; the number leads, as it is what is read back.
	const std::vector<std::pair<int, UINT>> FontWeights{
		{100, IDS_SETTINGS_FONT_WEIGHT_100}, {200, IDS_SETTINGS_FONT_WEIGHT_200}, {300, IDS_SETTINGS_FONT_WEIGHT_300}, {350, IDS_SETTINGS_FONT_WEIGHT_350},
		{400, IDS_SETTINGS_FONT_WEIGHT_400}, {500, IDS_SETTINGS_FONT_WEIGHT_500}, {600, IDS_SETTINGS_FONT_WEIGHT_600}, {700, IDS_SETTINGS_FONT_WEIGHT_700},
		{800, IDS_SETTINGS_FONT_WEIGHT_800}, {900, IDS_SETTINGS_FONT_WEIGHT_900}, {950, IDS_SETTINGS_FONT_WEIGHT_950},
	};
	const std::vector<std::pair<int, UINT>> FontStretches{
		{1, IDS_SETTINGS_FONT_STRETCH_1}, {2, IDS_SETTINGS_FONT_STRETCH_2}, {3, IDS_SETTINGS_FONT_STRETCH_3}, {4, IDS_SETTINGS_FONT_STRETCH_4}, {5, IDS_SETTINGS_FONT_STRETCH_5},
		{6, IDS_SETTINGS_FONT_STRETCH_6}, {7, IDS_SETTINGS_FONT_STRETCH_7}, {8, IDS_SETTINGS_FONT_STRETCH_8}, {9, IDS_SETTINGS_FONT_STRETCH_9},
	};
	const std::vector<std::pair<int, UINT>> FontStyles{
		{0, IDS_SETTINGS_FONT_STYLE_NORMAL}, {1, IDS_SETTINGS_FONT_STYLE_OBLIQUE}, {2, IDS_SETTINGS_FONT_STYLE_ITALIC},
	};

	std::wstring NamedValue(const XivAlexander::RuntimeConfigRepository& rt, const std::vector<std::pair<int, UINT>>& names, int value) {
		for (const auto& [v, name] : names) {
			if (v == value)
				return std::format(L"{} {}", v, rt.GetStringRes(name));
		}
		return std::format(L"{}", value);
	}

	// A press on the tree's empty area below its items moves the window, as the title bar would.
	constexpr UINT_PTR TreeSubclassId = 2;
	LRESULT CALLBACK TreeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
		if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK) {
			TVHITTESTINFO hit{.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}};
			TreeView_HitTest(hWnd, &hit);
			if (!hit.hItem) {
				ReleaseCapture();
				SendMessageW(GetAncestor(hWnd, GA_ROOT), WM_NCLBUTTONDOWN, HTCAPTION, 0);
				return 0;
			}
		} else if (uMsg == WM_NCDESTROY) {
			RemoveWindowSubclass(hWnd, TreeSubclassProc, TreeSubclassId);
		}
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// The tree's label edit box takes Enter and Esc, which the dialog would otherwise take first as OK and Cancel.
	constexpr UINT_PTR LabelEditSubclassId = 1;
	LRESULT CALLBACK LabelEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
		if (uMsg == WM_GETDLGCODE)
			return DefSubclassProc(hWnd, uMsg, wParam, lParam) | DLGC_WANTALLKEYS;
		if (uMsg == WM_NCDESTROY)
			RemoveWindowSubclass(hWnd, LabelEditSubclassProc, LabelEditSubclassId);
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	// Enter commits by leaving the box (EN_KILLFOCUS commits it); Escape empties it first, which cancels.
	constexpr UINT_PTR SessionRenameSubclassId = 2;
	LRESULT CALLBACK SessionRenameSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
		switch (uMsg) {
			case WM_GETDLGCODE:
				if (const auto msg = reinterpret_cast<const MSG*>(lParam); msg && msg->message == WM_KEYDOWN && (msg->wParam == VK_RETURN || msg->wParam == VK_ESCAPE))
					return DefSubclassProc(hWnd, uMsg, wParam, lParam) | DLGC_WANTMESSAGE;
				break;
			case WM_KEYDOWN:
				if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
					if (wParam == VK_ESCAPE)
						SetWindowTextW(hWnd, L"");
					SetFocus(GetParent(hWnd));
					return 0;
				}
				break;
			case WM_CHAR:
				if (wParam == L'\r' || wParam == L'\x1b')
					return 0;
				break;
			case WM_NCDESTROY:
				RemoveWindowSubclass(hWnd, SessionRenameSubclassProc, SessionRenameSubclassId);
				break;
		}
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

	std::vector<std::wstring> NamedValues(const XivAlexander::RuntimeConfigRepository& rt, const std::vector<std::pair<int, UINT>>& names) {
		std::vector<std::wstring> res;
		for (const auto& [v, name] : names)
			res.emplace_back(std::format(L"{} {}", v, rt.GetStringRes(name)));
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
	};

	ConfigItemBase* Item{};
	RowType Type = RowType::Unsupported;
	std::vector<std::wstring> ComboLabels;
	ItemInfo::FileKind PickFiles = ItemInfo::FileKind::None;

	HWND Label{};
	HWND Control{};
	HWND Button1{};
	HWND Button2{};
	HWND Button3{};
	HWND Button4{};
	HWND Description{};
	HWND Secondary{};  // The number of a slider.
	HWND Help{};  // A "(?)" after the label, drawn as a link; its text is the tooltip's, so that screen readers read it.
	HWND Status{};  // Of a check row: ItemInfo::Status.
	std::function<std::wstring()> GetStatus;

	std::unique_ptr<XivAlexander::Apps::MainApp::Window::GridView> Grid;
	int Height = 0;  // Of a grid, at 100% zoom.
	int FlowGroup = 0;  // Of a check row without a description: side by side with the rows before it of the same group.
	// Edit/combo rows without a description share a line per group: labels fit their text, controls are LineWidth (100% zoom), the last takes the rest.
	int LineGroup = 0;
	int LineWidth = 0;
	bool BesideCheck = false;  // Of an edit row: on the line of the check row before it, its label the unit after it.
	bool CommitOnChange = false;  // Of an edit row: as it is typed into, rather than when left.
	bool Bold = false;  // Of a check row that is a group's heading.
	bool Expanded = false;  // Of a rich text row: all of it shown.
	bool LinkButton = false;  // Button1 is drawn as a link.
	std::function<void(POINT ptScreen)> ContextMenu;  // A right click on the control.

	std::vector<HWND> Tabs;  // Of a tabs row: its buttons,
	int ActiveTab = 0;  // and which is chosen.
	const Row* TabOwner{};  // Of a row in a tab: the tabs row,
	int TabIndex = -1;  // and its tab.

	std::function<void()> Refresh;
	std::function<void()> Commit;
	// Takes the RowPart clicked.
	std::function<void(int)> Click;
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
			if (const auto value = ParseNumber<T>(GetWindowString(row.Control)))
				*item = *value;
			row.Refresh();
		};
		return true;
	}

	/// Opcodes: shown in hex as the opcode configuration has them; read in any base wcstoul accepts.
	template<typename TRow>
	bool BindHex16(TRow& row) {
		const auto item = dynamic_cast<XivAlexander::ConfigItem<uint16_t>*>(row.Item);
		if (!item)
			return false;
		row.Type = TRow::RowType::Edit;
		row.Refresh = [&row, item] { SetWindowTextW(row.Control, std::format(L"0x{:04x}", item->Value()).c_str()); };
		row.Commit = [&row, item] {
			const auto text = Trim(GetWindowString(row.Control));
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
			*item = xivres::util::unicode::convert<std::string>(GetWindowString(row.Control));
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
			*item = std::filesystem::path(Trim(GetWindowString(row.Control)));
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
				using FileKind = decltype(row.PickFiles);
				const auto purpose = row.PickFiles == FileKind::Dll ? std::string("ChainLoadDll") : std::format("PathList.{}", item->Name);
				const auto kind = row.PickFiles == FileKind::Dll ? PickKind::Dll : row.PickFiles == FileKind::Json ? PickKind::Json : PickKind::Folder;
				if (const auto path = PickPath(GetAncestor(row.Control, GA_ROOT), {}, kind, purpose)) {
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
			const auto text = Trim(GetWindowString(row.Control));
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

	template<typename T>
	std::vector<std::pair<T, std::wstring>> NamedChoices(std::initializer_list<T> values) {
		std::vector<std::pair<T, std::wstring>> res;
		for (const auto v : values)
			res.emplace_back(v, EnumName(v));
		return res;
	}
}

namespace {
	/// The edit of an editable combo box is its child.
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

XivAlexander::Apps::MainApp::Window::SettingsView::SettingsView(App& app, HWND hWndParent, HWND hWndOwner, std::function<double()> getZoom, std::function<bool()> isDarkMode, std::function<void(HDC, const RECT&)> paintStatus, RestartChoices restartChoices)
	: m_app(app)
	, m_config(Config::Acquire())
	, m_hWndOwner(hWndOwner)
	, m_getZoom(std::move(getZoom))
	, m_isDarkMode(std::move(isDarkMode))
	, m_paintStatus(std::move(paintStatus))
	, m_restartChoices(std::move(restartChoices)) {
	InitializeItemInfo();

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
	SetWindowSubclass(m_hTree, TreeSubclassProc, TreeSubclassId, 0);

	m_hPage = create(m_hWnd, WS_EX_CONTROLPARENT, PageClassName, WS_VSCROLL | WS_CLIPCHILDREN);
	SetWindowLongPtrW(m_hPage, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	m_hStatus = create(m_hWnd, 0, StatusClassName, WS_VISIBLE);
	SetWindowLongPtrW(m_hStatus, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	// Owned by the page, which it goes with.
	m_hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hPage, nullptr, Dll::Module(), nullptr);
	if (m_hTooltip) {
		SendMessageW(m_hTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
		TTTOOLINFOW ti{.cbSize = sizeof ti, .uFlags = TTF_TRACK | TTF_ABSOLUTE, .hwnd = m_hPage, .uId = TrackedHelpToolId, .lpszText = const_cast<LPWSTR>(L"")};
		SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
	}

	// The labels of version-sensitive items are marked while those features are off for this session.
	m_cleanup += m_config->Runtime.OnVersionSensitiveFeaturesAllowedChange([this] { PostMessageW(m_hWnd, WmRebuildPage, 0, 0); });

	// The latency helper reporting cooldowns comes and goes with its setting; listen again after the app has made or removed it.
	m_cleanup += m_config->Runtime.NetworkTiming.Enabled.AddAndCallOnChange([this] { PostMessageW(m_hWnd, WmListenToCooldowns, 0, 0); });

	// So does CrowdFix, whose statuses the page shows: listened to while they are shown (ListenToCrowdFix).
	m_cleanup += m_config->Runtime.CrowdFix.Enabled.OnChange([this] { PostMessageW(m_hWnd, WmListenToCrowdFix, 0, 0); });

	// The ModPacks are loaded on another thread after this, and their folders go in the tree.
	m_cleanup += m_app.GetResourceOverrider().OnVirtualSqPacksInitialized([this] { PostMessageW(m_hWnd, WmTtmpSetsChanged, 0, 0); });
	ListenToTtmps();

	RefreshTheme();
	UpdateFont(GetZoom());
	PopulateTree();
}

struct XivAlexander::Apps::MainApp::Window::SettingsView::GameDirectoryDetection {
	struct Result {
		bool Found = false;
		xivres::game_release_publisher Region{};
		std::string CountryCode;
		std::string Version;
	};

	std::mutex Mutex;
	HWND Owner{};  // Cleared when the view goes.
	std::map<std::filesystem::path, std::optional<Result>> Results;  // Empty while being detected.

	static Result Detect(const std::filesystem::path& gameDir) {
		// The folder holding sqpack and ffxivgame.ver, as the text merger reads it; GetGameReleaseInfo would also look in its parents.
		std::error_code ec;
		if (!exists(gameDir / L"sqpack" / L"ffxiv" / L"0a0000.win32.index", ec) || !exists(gameDir / L"ffxivgame.ver", ec))
			return {};
		try {
			auto info = Misc::GameInstallationDetector::GetGameReleaseInfo(gameDir);
			if (!equivalent(info.GamePath(), gameDir, ec))
				return {};
			return {true, info.Region, std::move(info.CountryCode), xivres::util::trim(info.GameVersion)};
		} catch (const std::exception&) {
			return {};
		}
	}
};

XivAlexander::Apps::MainApp::Window::SettingsView::~SettingsView() {
	if (m_gameDirectories) {
		const auto lock = std::scoped_lock(m_gameDirectories->Mutex);
		m_gameDirectories->Owner = nullptr;
	}
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

void XivAlexander::Apps::MainApp::Window::SettingsView::RefreshRowStatuses() {
	auto changed = false;
	for (const auto& row : m_rows) {
		if (!row->Status)
			continue;
		const auto text = row->GetStatus();
		if (text == GetWindowString(row->Status))
			continue;
		SetWindowTextW(row->Status, text.c_str());
		changed = true;
	}
	if (changed)
		LayoutPage();
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
	label(rt.FontReplacement.Edge, IDS_SETTINGS_EDGE_USE);
	m_itemInfo[&rt.FontReplacement.Edge].AfterInlineGroups = true;
	label(rt.FontReplacement.ClearType, IDS_SETTINGS_SUBPIXEL_USE);
	m_itemInfo[&rt.FontReplacement.ClearType].AfterInlineGroups = true;

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

	label(rt.Ui.MainWindow.NotifyWhenHidden, IDS_SETTINGS_NOTIFYWHENHIDDEN);
	label(rt.FramerateControl.BackgroundLimit, IDS_SETTINGS_BACKGROUNDLIMIT);
	m_itemInfo[&rt.FramerateControl.BackgroundLimit].BesideCheck = true;
	label(rt.FramerateControl.Lock.Interval, IDS_SETTINGS_LOCK_INTERVAL);
	label(rt.FramerateControl.Lock.Automatic, IDS_SETTINGS_LOCK_AUTOMATIC);
	label(rt.FramerateControl.Lock.TargetFramerateRangeFrom, IDS_SETTINGS_LOCK_RANGEFROM);
	label(rt.FramerateControl.Lock.TargetFramerateRangeTo, IDS_SETTINGS_LOCK_RANGETO);
	label(rt.FramerateControl.Lock.MaximumRenderIntervalDeviation, IDS_SETTINGS_LOCK_MAXDEVIATION);
	label(rt.FramerateControl.Lock.GlobalCooldown, IDS_SETTINGS_LOCK_GCD);
	label(rt.Modding.Languages.ResourceOverride, IDS_SETTINGS_RESOURCEOVERRIDE, IDS_SETTINGS_RESOURCEOVERRIDE_DESC);
	label(rt.Modding.Languages.VoiceResourceOverride, IDS_SETTINGS_VOICERESOURCEOVERRIDE, IDS_SETTINGS_VOICERESOURCEOVERRIDE_DESC);
	label(rt.Modding.Languages.FallbackPriority, IDS_SETTINGS_FALLBACKPRIORITY);
	label(rt.Modding.Languages.ForcedCharacterLanguages, IDS_SETTINGS_FORCEDCHARACTERLANGUAGES);
	label(rt.Modding.Languages.TalkLines, IDS_SETTINGS_TALKLINES, IDS_SETTINGS_TALKLINES_DESC);
	label(rt.Modding.TextTransformationFiles, IDS_SETTINGS_EXCEL_OVERRIDEFILES, IDS_SETTINGS_EXCEL_OVERRIDEFILES_DESC);
	label(rt.Modding.Logging.ExcelMerge, IDS_SETTINGS_LOGGING_EXCELMERGE);

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
	{
		using Features::CrowdFix;
		const auto statusText = [this](const std::function<CrowdFix::FixStatus(const CrowdFix&)>& get) -> std::wstring {
			const auto crowdFix = m_app.GetCrowdFix();
			if (!crowdFix)
				return {};
			switch (get(*crowdFix).State) {
				case CrowdFix::FixState::On: return m_config->Runtime.GetStringRes(IDS_SETTINGS_CROWDFIX_STATUS_ACTIVE);
				case CrowdFix::FixState::Off: return m_config->Runtime.GetStringRes(IDS_SETTINGS_CROWDFIX_STATUS_OFF);
				case CrowdFix::FixState::Waiting: return m_config->Runtime.GetStringRes(IDS_SETTINGS_CROWDFIX_STATUS_WAITING);
				case CrowdFix::FixState::Unavailable: return m_config->Runtime.GetStringRes(IDS_SETTINGS_CROWDFIX_STATUS_UNAVAILABLE);
				case CrowdFix::FixState::Failed: return m_config->Runtime.GetStringRes(IDS_SETTINGS_CROWDFIX_STATUS_FAILED);
			}
			return {};
		};
		m_itemInfo[&rt.CrowdFix.Enabled].Status = [statusText] { return statusText([](const CrowdFix& c) { return c.GetStatus(); }); };
		for (size_t i = 0; i < static_cast<size_t>(CrowdFix::Fix::Count); i++) {
			const auto fix = static_cast<CrowdFix::Fix>(i);
			m_itemInfo[&CrowdFix::ConfigItemOf(rt, fix)].Status = [statusText, fix] { return statusText([fix](const CrowdFix& c) { return c.GetStatus(fix); }); };
		}
	}

	// What most need is shown below an item; details, behind its "(?)".
	const auto describe = [this](const ConfigItemBase& item, UINT descriptionId) { m_itemInfo[&item].DescriptionId = descriptionId; };
	const auto tip = [this](const ConfigItemBase& item, UINT tooltipId) { m_itemInfo[&item].TooltipId = tooltipId; };
	for (const auto item : fixes.Items()) {
		auto& info = m_itemInfo[item];
		info.TooltipId = std::exchange(info.DescriptionId, 0);
	}
	tip(rt.Modding.Languages.TalkLines, std::exchange(m_itemInfo[&rt.Modding.Languages.TalkLines].DescriptionId, 0));
	tip(rt.Modding.Languages.VoiceResourceOverride, IDS_SETTINGS_VOICELIPSYNC_TIP);

	label(rt.Ui.Language, IDS_SETTINGS_UI_LANGUAGE);
	label(rt.Ui.ThemeMode, IDS_SETTINGS_UI_THEMEMODE);

	label(rt.GameWindow.TitleMode, IDS_SETTINGS_GAMEWINDOW_TITLEMODE);
	label(rt.GameWindow.TitlePrefixFormat, IDS_SETTINGS_GAMEWINDOW_TITLEPREFIX);
	m_itemInfo[&rt.GameWindow.TitlePrefixFormat].NoteId = IDS_SETTINGS_TITLEFORMAT_NOTE;
	label(rt.GameWindow.TitleSuffixFormat, IDS_SETTINGS_GAMEWINDOW_TITLESUFFIX);
	label(rt.GameWindow.DisableGhosting, IDS_SETTINGS_GAMEWINDOW_DISABLEGHOSTING, IDS_SETTINGS_DISABLEGHOSTING_DESC);
	label(rt.GameWindow.UseImeModeIndicator, IDS_SETTINGS_GAMEWINDOW_IMEMODEINDICATOR);
	tip(rt.GameWindow.UseImeModeIndicator, IDS_SETTINGS_IMEMODEINDICATOR_TIP);

	describe(rt.NetworkTiming.Enabled, IDS_SETTINGS_NETWORK_ENABLED_DESC);
	label(rt.NetworkTiming.HighLatencyMitigationMode, IDS_SETTINGS_NETWORK_MITIGATIONMODE);
	tip(rt.NetworkTiming.HighLatencyMitigationMode, IDS_SETTINGS_MITIGATIONMODE_TIP);
	tip(rt.NetworkTiming.UseHighLatencyMitigationLogging, IDS_SETTINGS_NETWORK_LOGGING_TIP);
	describe(rt.NetworkTiming.UseHighLatencyMitigationPreviewMode, IDS_SETTINGS_NETWORK_PREVIEW_DESC);
	tip(rt.Socket.ReducePacketDelay, IDS_SETTINGS_REDUCEPACKETDELAY_TIP);
	m_itemInfo[&rt.Socket.TakeOverLoopbackAddresses].NoteId = IDS_SETTINGS_SOCKET_TAKEOVER_NOTE;
	tip(rt.Opcodes.UseOpcodeFinder, IDS_SETTINGS_OPCODEFINDER_TIP);
	m_itemInfo[&rt.Opcodes].AppendDescriptionId = IDS_SETTINGS_OPCODECONFIG_DESC;
	tip(m_config->Game.Common_UseOodleTcp, IDS_SETTINGS_OODLETCP_TIP);
	tip(m_config->Game.Server_IpRange, IDS_SETTINGS_SERVERIPRANGE_TIP);
	tip(m_config->Game.Server_PortRange, IDS_SETTINGS_SERVERPORTRANGE_TIP);

	tip(rt.FramerateControl.UseMoreCpuTime, IDS_SETTINGS_USEMORECPUTIME_TIP);
	tip(rt.FramerateControl.SynchronizeProcessing, IDS_SETTINGS_SYNCHRONIZEPROCESSING_TIP);
	tip(rt.FramerateControl.Lock.MaximumRenderIntervalDeviation, IDS_SETTINGS_LOCK_MAXDEVIATION_TIP);

	label(rt.Modding.AdditionalSqpackRootDirectories, IDS_SETTINGS_MODDING_SQPACKROOTS);
	label(rt.Modding.GameResourceFileEntryRootDirectories, IDS_SETTINGS_MODDING_FILEENTRYROOTS);
	label(rt.Modding.PathReplacements, IDS_SETTINGS_MODDING_PATHREPLACEMENTS);
	label(rt.Modding.Ttmp.SearchDirectories, IDS_SETTINGS_TTMP_SEARCHDIRS);
	label(rt.Modding.Ttmp.ChoicesFiles, IDS_SETTINGS_TTMP_CHOICESFILES);
	tip(rt.Modding.UseAltCodecMusicSupport, IDS_SETTINGS_ALTCODEC_TIP);
	label(rt.Modding.Logging.HashTrackerKeys, IDS_SETTINGS_LOGGING_HASHTRACKERKEYS);
	label(rt.Modding.Logging.AllPaths, IDS_SETTINGS_LOGGING_ALLPATHS);
	label(rt.Modding.Logging.ReplacedPaths, IDS_SETTINGS_LOGGING_REPLACEDPATHS);
	label(rt.Modding.Logging.DialogueCharacterNames, IDS_SETTINGS_LOGGING_DIALOGUENAMES);
	label(rt.Modding.Logging.PathFilters, IDS_SETTINGS_LOGGING_PATHFILTERS);
	tip(rt.Modding.Logging.AllDataFileRead, IDS_SETTINGS_LOGGING_ALLDATAFILEREAD_TIP);
	tip(rt.Modding.Logging.HashTrackerKeys, IDS_SETTINGS_LOGGING_HASHTRACKERKEYS_TIP);
	tip(rt.Modding.Logging.AllPaths, IDS_SETTINGS_LOGGING_ALLPATHS_TIP);
	tip(rt.Modding.Logging.ReplacedPaths, IDS_SETTINGS_LOGGING_REPLACEDPATHS_TIP);
	tip(rt.Modding.Logging.DialogueCharacterNames, IDS_SETTINGS_LOGGING_DIALOGUENAMES_TIP);

	describe(rt.Audio.OutputSamplingRate, IDS_SETTINGS_SAMPLINGRATE_DESC);
	auto& soxr = rt.Audio.SoxrResampler.Filter;
	tip(rt.Audio.SoxrResampler.Enabled, IDS_SETTINGS_SOXR_ENABLED_TIP);
	tip(soxr, IDS_SETTINGS_SOXR_FILTER_TIP);
	tip(soxr.Quality, IDS_SETTINGS_SOXR_QUALITY_TIP);
	tip(soxr.Phase, IDS_SETTINGS_SOXR_PHASE_TIP);
	tip(soxr.SteepFilter, IDS_SETTINGS_SOXR_STEEPFILTER_TIP);
	tip(soxr.PassbandEnd, IDS_SETTINGS_SOXR_PASSBANDEND_TIP);
	tip(soxr.StopbandBegin, IDS_SETTINGS_SOXR_STOPBANDBEGIN_TIP);
	tip(soxr.PassbandRolloff, IDS_SETTINGS_SOXR_ROLLOFF_TIP);
	tip(soxr.DoublePrecision, IDS_SETTINGS_SOXR_DOUBLEPRECISION_TIP);
	tip(soxr.HighPrecisionClock, IDS_SETTINGS_SOXR_HIGHPRECISIONCLOCK_TIP);
	tip(soxr.Log2MinDftSize, IDS_SETTINGS_SOXR_LOG2MINDFT_TIP);
	tip(soxr.Log2LargeDftSize, IDS_SETTINGS_SOXR_LOG2LARGEDFT_TIP);

	label(rt.FontReplacement.Enabled, IDS_SETTINGS_FONT_ENABLED);
	tip(rt.FontReplacement.Enabled, IDS_SETTINGS_FONT_ENABLED_TIP);
	label(rt.FontReplacement.Faces.SystemFallback, IDS_SETTINGS_FONT_SYSTEMFALLBACK);
	tip(rt.FontReplacement.Faces.SystemFallback, IDS_SETTINGS_FONT_SYSTEMFALLBACK_TIP);
	label(rt.FontReplacement.Subpixel, IDS_SETTINGS_SUBPIXEL);
	label(rt.FontReplacement.VerticalSubpixel, IDS_SETTINGS_SUBPIXEL_VERTICAL);
	label(rt.FontReplacement.NamePlates, IDS_SETTINGS_NAMEPLATES);
	tip(rt.FontReplacement.NamePlates, IDS_SETTINGS_NAMEPLATES_TIP);

	// Named as the files they stand in for, in every language.
	m_itemInfo[&rt.ChainLoad.D3d11].FixedLabel = L"d3d11.dll";
	m_itemInfo[&rt.ChainLoad.Dxgi].FixedLabel = L"dxgi.dll";
	m_itemInfo[&rt.ChainLoad.Dinput8].FixedLabel = L"dinput8.dll";
	describe(rt.ChainLoad, IDS_SETTINGS_CHAINLOAD_DESC);

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
		static_cast<ConfigItemBase*>(&rt.Modding.TextTransformationFiles),
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
	label(rt.Launch.ClearCopiedCommandLineSeconds, IDS_SETTINGS_LAUNCH_CLEARCOPIED);
	tip(rt.Launch.ClearCopiedCommandLineSeconds, IDS_SETTINGS_LAUNCH_CLEARCOPIED_DESC);
	m_itemInfo[&rt.Opcodes].AppendNode = &m_config->Game;
	m_itemInfo[&rt.Opcodes].AppendHeadingId = IDS_SETTINGS_HEADING_OPCODECONFIG;

	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.ChainLoad.D3d11),
		static_cast<ConfigItemBase*>(&rt.ChainLoad.Dxgi),
		static_cast<ConfigItemBase*>(&rt.ChainLoad.Dinput8),
	})
		m_itemInfo[item].PickFiles = ItemInfo::FileKind::Dll;

	// Worked out by the repository, set by the version prompt, or shown on their own page; animation lock durations are left to editing the file.
	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.Launch.RememberedLanguage),
		static_cast<ConfigItemBase*>(&rt.Launch.RememberedRegion),
		static_cast<ConfigItemBase*>(&rt.Launch.UseLoginSessionSwitching),
		static_cast<ConfigItemBase*>(&rt.Ui.MainWindow.SettingsTreeWidth),
		static_cast<ConfigItemBase*>(&rt.Opcodes.CheckForUpdatesOnStartup),
		static_cast<ConfigItemBase*>(&rt.NetworkTiming.ExpectedAnimationLockDurationUs),
		static_cast<ConfigItemBase*>(&rt.FramerateControl.UseMainThreadTimingHandler),
		static_cast<ConfigItemBase*>(&rt.Opcodes.VersionSensitiveFeaturesAllowedGameVersion),
		static_cast<ConfigItemBase*>(&rt.Opcodes.EnabledPatchCodes),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.Faces.Families),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.Atlas),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.Subpixel),
		static_cast<ConfigItemBase*>(&rt.FontReplacement.VerticalSubpixel),
	})
		m_itemInfo[item].Hidden = true;

	for (const ConfigItemBase* item : {
		static_cast<ConfigItemBase*>(&rt.Launch.UseLoginSessionSwitching),
		static_cast<ConfigItemBase*>(&rt.Modding.Enabled),
		static_cast<ConfigItemBase*>(&rt.Opcodes.EnabledPatchCodes),
		static_cast<ConfigItemBase*>(&rt.Modding.UseAltCodecMusicSupport),
		static_cast<ConfigItemBase*>(&rt.GameWindow.UseImeModeIndicator),
		static_cast<ConfigItemBase*>(&rt.Modding.Languages.ResourceOverride),
		static_cast<ConfigItemBase*>(&rt.Modding.Languages.TalkLines),
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
			}, .AddFirst = [this, commands] {
				commands({ID_NETWORK_RESETALLCONNECTIONS});
				AddHelp(m_rows.size() - 1, IDS_SETTINGS_RESETCONNECTIONS_TIP);
			}},
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
		if (info->FixedLabel)
			label = info->FixedLabel;
		else if (info->LabelId)
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
	m_crowdFixListener.clear();
	m_deferredTabbed.clear();
	TruncateRows(0);
	m_ttmpPage.reset();
	m_scrollY = 0;
}

void XivAlexander::Apps::MainApp::Window::SettingsView::TruncateRows(size_t count) {
	for (auto i = count; i < m_rows.size(); ++i) {
		const auto& row = m_rows[i];
		if (row->Help) {
			if (row->Help == m_trackedHelp)
				ShowHelp(nullptr, false);
			TTTOOLINFOW ti{.cbSize = sizeof ti, .hwnd = m_hPage, .uId = reinterpret_cast<UINT_PTR>(row->Help)};
			SendMessageW(m_hTooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
		}
		for (const auto hWnd : {row->Label, row->Control, row->Button1, row->Button2, row->Button3, row->Button4, row->Description, row->Secondary, row->Help, row->Status}) {
			if (hWnd)
				DestroyWindow(hWnd);
		}
		for (const auto hWnd : row->Tabs)
			DestroyWindow(hWnd);
	}
	if (count < m_rows.size())
		m_rows.erase(m_rows.begin() + static_cast<ptrdiff_t>(count), m_rows.end());
}

xivres::util::on_dtor XivAlexander::Apps::MainApp::Window::SettingsView::FreezePage() {
	if (!m_hPage)
		return {};
	if (m_pageFreezes++ == 0)
		SendMessageW(m_hPage, WM_SETREDRAW, FALSE, 0);
	return xivres::util::on_dtor([this] {
		if (--m_pageFreezes != 0 || !m_hPage)
			return;
		// WM_SETREDRAW TRUE makes the page visible, even if the status took its place meanwhile.
		SendMessageW(m_hPage, WM_SETREDRAW, TRUE, 0);
		if (m_hStatus && (GetWindowLongPtrW(m_hStatus, GWL_STYLE) & WS_VISIBLE))
			ShowWindow(m_hPage, SW_HIDE);
		else
			RedrawWindow(m_hPage, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	});
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ShowPage(const TreeNode* pNode) {
	// A node without a page of its own shows its first child's; it stays selected, so that the arrow keys go past it.
	auto pShown = pNode;
	while (pShown && pShown->Kind == TreeNode::NodeKind::Groups && pShown->Groups.empty() && !pShown->Children.empty())
		pShown = &pShown->Children.front();

	// Shown or hidden before the page stops drawing, as WM_SETREDRAW changes its visibility too.
	const auto status = pShown && pShown->Kind == TreeNode::NodeKind::Status;
	ShowWindow(m_hPage, status ? SW_HIDE : SW_SHOW);
	ShowWindow(m_hStatus, status ? SW_SHOW : SW_HIDE);

	// Drawn once, when built and laid out.
	xivres::util::on_dtor freeze;
	if (!status)
		freeze = FreezePage();
	ClearRows();
	m_pNode = pNode;
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
	ListenToCrowdFix();
	RefreshRows();
	LayoutPage();
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ListenToCrowdFix() {
	m_crowdFixListener.clear();
	if (std::ranges::none_of(m_rows, [](const auto& row) { return !!row->Status; }))
		return;
	if (const auto crowdFix = m_app.GetCrowdFix())
		m_crowdFixListener = crowdFix->OnStatusChange([hWnd = m_hWnd] { PostMessageW(hWnd, WmCrowdFixStatusChanged, 0, 0); });
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
		if (info)
			AddHelp(m_rows.size() - 1, info->TooltipId);
	}
	if (info && info->DescriptionId)
		CreateNoteRow(m_config->Runtime.GetStringRes(info->DescriptionId));

	// Two buttons per row.
	if (info) {
		for (size_t i = 0; i < info->Actions.size(); i += 2) {
			ActionButtons buttons;
			for (auto j = i; j < std::min(i + 2, info->Actions.size()); ++j) {
				const auto [labelId, commandId] = info->Actions[j];
				buttons.emplace_back(
					labelId ? std::wstring(m_config->Runtime.GetStringRes(labelId)) : GetMenuText(IDR_TRAY_MENU, commandId),
					[this, commandId] { PostMessageW(m_hWndOwner, WM_COMMAND, commandId, 0); });
			}
			CreateActionRow(std::move(buttons));
		}
	}

	AddGroupRows(group);

	if (info && info->AppendNode) {
		CreateHeadingRow(m_config->Runtime.GetStringRes(info->AppendHeadingId));
		if (info->AppendDescriptionId)
			CreateNoteRow(m_config->Runtime.GetStringRes(info->AppendDescriptionId));
		AddGroupRows(*info->AppendNode);
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddGroupRows(const ConfigNode& node) {
	// Check boxes go side by side only with those of the same group, identified by its address.
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
		if (const auto info = FindItemInfo(*item); info && info->AfterInlineGroups)
			continue;
		CreateRow(*item);
		if (flowGroup && !m_rows.empty() && m_rows.back()->Item == item && m_rows.back()->Type == Row::RowType::Check && !m_rows.back()->Description)
			m_rows.back()->FlowGroup = flowGroup;
	}

	for (const auto item : node.Items()) {
		const auto group = dynamic_cast<const ConfigGroup*>(item);
		if (const auto info = group ? FindItemInfo(*group) : nullptr; info && info->Inline)
			AddGroupSection(*group, true);
	}

	for (const auto item : node.Items()) {
		if (const auto info = FindItemInfo(*item); info && info->AfterInlineGroups && !info->Hidden && !info->Tabbed && !dynamic_cast<const ConfigGroup*>(item))
			CreateRow(*item);
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddPatchCodeRows() {
	auto& rt = m_config->Runtime;
	auto& enabled = rt.Opcodes.EnabledPatchCodes;
	const auto entries = m_config->PatchCode.GetEntries();

	// A fix is enabled by the digest of its contents, so renaming its file keeps it enabled.
	const auto mark = rt.AreVersionSensitiveFeaturesDisabledTemporarily() ? L"(!) " : L"";
	if (*mark)
		CreateNoteRow(rt.GetStringRes(IDS_SETTINGS_VERSIONSENSITIVE_TIP));
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
		.GetChecked = [entries, &enabled](size_t row, size_t) { const auto digests = enabled.Snapshot(); return std::ranges::find(*digests, (*entries)[row].Digest) != digests->end(); },
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
			const auto& rt = m_config->Runtime;
			ShowPopupMenu(m_hWnd, ptScreen, {
				{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_EDIT), [&] { OpenPatchCodeEditor(entry.Path.filename().wstring(), entry.Path); }, true, true},
				{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_DELETE), [&] { DeletePatchCode(entry.Digest, entry.Path); }},
				{},
				{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [&] { OpenInExplorer(m_hWndOwner, entry.Path); }},
			});
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
	PostOnChange(m_config->PatchCode, WmRebuildPage);
	PostOnChange(enabled, WmRefreshRows);
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
	CreateActionRow({
		{std::format(L"{} / {}...", title(IDS_RESTART_GAME_RESTART), title(IDS_RESTART_GAME_NEWINSTANCE)), [this] { PostMessageW(m_hWndOwner, WM_COMMAND, ID_RESTART_RESTART, 0); }},
		{GetMenuText(IDR_TRAY_MENU, ID_RESTART_COPYLAUNCHCOMMANDLINE), [this] { PostMessageW(m_hWndOwner, WM_COMMAND, ID_RESTART_COPYLAUNCHCOMMANDLINE, 0); }},
	});

	// What the next restart uses, as the menu had them, each kind on its own lines; unlike the menu, choosing one doesn't restart.
	const auto choice = [this](UINT commandId, bool checkBox, int flowGroup) {
		CreateChoiceRow(GetMenuText(IDR_TRAY_MENU, commandId), m_restartChoices.GetState(commandId).second,
			[this, commandId] { return m_restartChoices.GetState(commandId).first; },
			[this, commandId] { m_restartChoices.Choose(commandId); },
			checkBox, flowGroup);
	};
	for (const auto commandId : {ID_RESTART_USEXIVALEXANDER, ID_RESTART_USEPARAMETEROBFUSCATION, ID_RESTART_USEELEVATION}) {
		choice(commandId, true, 1);
		if (commandId == ID_RESTART_USEPARAMETEROBFUSCATION)
			AddHelp(m_rows.size() - 1, IDS_SETTINGS_RESTART_OBFUSCATION_TIP);
	}

	// Languages and regions that can't be chosen (other publishers', or disallowed by the installation) are hidden; so is Remember if none can be.
	const auto choices = [this, &rt, &choice](const std::wstring& heading, std::initializer_list<UINT> commandIds, UINT rememberId, int flowGroup) {
		if (std::ranges::none_of(commandIds, [this](UINT commandId) { return m_restartChoices.GetState(commandId).second; }))
			return;
		CreateHeadingRow(heading, rt.GetStringRes(IDS_SETTINGS_REMEMBER), m_restartChoices.GetState(rememberId).second,
			[this, rememberId] { return m_restartChoices.GetState(rememberId).first; },
			[this, rememberId] { m_restartChoices.Choose(rememberId); });
		AddHelp(m_rows.size() - 1, IDS_SETTINGS_REMEMBER_TIP);
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

	// Forgetting the stored sessions is by the heading, asked first; the launch arguments' session isn't stored.
	auto anyStored = false;
	if (const auto loginSessions = m_app.GetLoginSessions()) {
		PostOnChange(*loginSessions, WmRebuildPage);
		anyStored = std::ranges::any_of(loginSessions->GetSessions(), [](const auto& session) { return !session.Alias.empty(); });
	}
	// Switching sessions in the running game is a separate setting, turned on only by editing the configuration file.
	if (m_app.GetLoginSessions()) {
		const auto switchingHeld = rt.Launch.UseLoginSessionSwitching && rt.AreVersionSensitiveFeaturesDisabledTemporarily();
		CreateHeadingRow(std::format(L"{}{}", switchingHeld ? L"(!) " : L"", CleanMenuText(rt.GetStringRes(IDS_MENU_LOGINSESSION))),
			std::wstring(), false, {}, {},
			anyStored ? std::wstring(rt.GetStringRes(IDS_SETTINGS_SESSIONS_FORGET)) : std::wstring(),
			anyStored ? std::function<void()>([this] {
				if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.GetStringRes(IDS_SETTINGS_SESSIONS_FORGET_CONFIRM)) != IDYES)
					return;
				if (const auto sessions = m_app.GetLoginSessions())
					sessions->ForgetStoredSessions();
			}) : std::function<void()>());
		if (switchingHeld)
			AddHelp(m_rows.size() - 1, IDS_SETTINGS_VERSIONSENSITIVE_TIP);
	}
	AddSessionRows();

	CreateHeadingRow(rt.GetStringRes(IDS_SETTINGS_MISCELLANEOUS));
	CreateRow(rt.Launch.ClearCopiedCommandLineSeconds);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddSessionRows() {
	auto& rt = m_config->Runtime;
	PostOnChange(rt.Launch.UseLoginSessionSwitching, WmRebuildPage);

	// By name, the unnamed launch session first.
	const auto loginSessions = m_app.GetLoginSessions();
	if (!loginSessions)
		return;
	PostOnChange(*loginSessions, WmRebuildPage);

	const auto sessions = loginSessions->GetSessions();

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
				const auto loginSessions = m_app.GetLoginSessions();
				return loginSessions && loginSessions->GetSelectedIndex() == index;
			},
			[this, index = k] {
				if (const auto loginSessions = m_app.GetLoginSessions())
					loginSessions->Select(index);
			},
			false, 1);
		if (alias.empty())
			continue;
		m_rows.back()->ContextMenu = [this, &row = *m_rows.back(), alias = xivres::util::unicode::convert<std::string>(alias), label](POINT ptScreen) {
			ShowPopupMenu(m_hWnd, ptScreen, {
				{m_config->Runtime.GetStringRes(IDS_SETTINGS_SESSIONS_RENAME), [&] { BeginSessionRename(row, alias); }},
				{m_config->Runtime.GetStringRes(IDS_SETTINGS_SESSIONS_FORGETONE), [&] {
					if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.FormatStringRes(IDS_SETTINGS_SESSIONS_FORGETONE_CONFIRM, label)) != IDYES)
						return;
					if (const auto loginSessions = m_app.GetLoginSessions())
						loginSessions->ForgetStoredSession(alias);
				}},
			});
		};
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::BeginSessionRename(Row& row, const std::string& alias) {
	if (row.Secondary) {
		SetFocus(row.Secondary);
		return;
	}

	// A box in place of the button, as a slider's number is: PartSecondary commits it on EN_KILLFOCUS, and the row destroys it.
	const auto index = static_cast<size_t>(std::ranges::find(m_rows, &row, &std::unique_ptr<Row>::get) - m_rows.begin());
	row.Secondary = CreateRowControl(PartSecondary, WS_EX_CLIENTEDGE, WC_EDITW, xivres::util::unicode::convert<std::wstring>(alias), WS_TABSTOP | ES_AUTOHSCROLL, index);
	SetWindowSubclass(row.Secondary, SessionRenameSubclassProc, SessionRenameSubclassId, 0);
	SendMessageW(row.Secondary, EM_SETLIMITTEXT, 64, 0);
	ShowWindow(row.Control, SW_HIDE);

	row.Commit = [this, &row, alias, done = std::make_shared<bool>(false)] {
		if (std::exchange(*done, true))
			return;
		const auto newAlias = Utf8(Trim(GetWindowString(row.Secondary)));
		// The page is built anew either way; a message box may let that happen before it returns, so nothing of the row is used after.
		const auto self = this;
		const auto oldAlias = alias;
		PostMessageW(m_hWnd, WmRebuildPage, 0, 0);
		if (newAlias.empty() || newAlias == oldAlias)
			return;
		try {
			// Not held over the message boxes.
			const auto result = [&] {
				const auto loginSessions = self->m_app.GetLoginSessions();
				return loginSessions ? loginSessions->RenameStoredSession(oldAlias, newAlias) : Features::LoginSessions::RenameResult::NotFound;
			}();
			switch (result) {
				case Features::LoginSessions::RenameResult::InvalidAlias:
					Dll::MessageBoxF(self->m_hWndOwner, MB_OK | MB_ICONWARNING, IDS_SETTINGS_SESSIONS_RENAME_INVALID);
					break;
				case Features::LoginSessions::RenameResult::AlreadyExists:
					Dll::MessageBoxF(self->m_hWndOwner, MB_OK | MB_ICONWARNING, self->m_config->Runtime.FormatStringRes(IDS_SETTINGS_SESSIONS_RENAME_EXISTS, xivres::util::unicode::convert<std::wstring>(newAlias)));
					break;
				default:
					break;
			}
		} catch (const std::exception& e) {
			Dll::MessageBoxF(self->m_hWndOwner, MB_OK | MB_ICONERROR, IDS_ERROR_UNEXPECTED, e.what());
		}
	};

	LayoutPage();
	SetFocus(row.Secondary);
	SendMessageW(row.Secondary, EM_SETSEL, 0, -1);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddFramerateLockingRows() {
	auto& rt = m_config->Runtime;
	auto& lock = rt.FramerateControl.Lock;

	// Disabled, a fixed framerate, or a framerate chosen from the cooldown as it is measured.
	const auto getMode = [&lock] { return lock.Automatic ? 2 : lock.Interval ? 1 : 0; };
	const auto mode = getMode();

	// The global cooldown the game last reported, or UINT64_MAX.
	const auto measuredCooldownUs = [this] {
		if (const auto handler = m_app.GetNetworkTimingHandler())
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
	CreateActionRow({{rt.GetStringRes(IDS_SETTINGS_OPENFRAMERATELOCKING), [this] { PostMessageW(m_hWndOwner, WM_COMMAND, ID_CONFIGURE_LOCKFRAMERATE, 0); }}});

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
	AddDescription(m_rows.size() - 1, IDS_SETTINGS_LOCK_MODE_DESC);

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
		CreateActionRow({{rt.GetStringRes(IDS_SETTINGS_LOCK_MINIMIZECLIPPING), [&lock, bestIntervalUs] {
			lock.Interval = bestIntervalUs(lock.GlobalCooldown * 10000);
		}}});
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
		setText(GetWindowString(row.Control));
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

XivAlexander::Apps::MainApp::Window::GridView& XivAlexander::Apps::MainApp::Window::SettingsView::CreateListGridRow(ConfigItemBase& item, UINT descriptionId, int height, std::vector<GridView::Column> columns, GridView::Source source) {
	auto& grid = CreateGridRow(GetItemLabel(item), descriptionId, height, std::move(columns), std::move(source));
	PostOnChange(item, WmRefreshRows);
	return grid;
}

template<typename T>
void XivAlexander::Apps::MainApp::Window::SettingsView::PostOnChange(T& source, UINT message) {
	m_rowCleanup += source.OnChange([this, message] { PostMessageW(m_hWnd, message, 0, 0); });
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
		if (const auto value = ParseNumber<double>(GetWindowString(row.Secondary)))
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
	auto& grid = CreateListGridRow(item, IDS_SETTINGS_PATHREPLACEMENT_DESC, 200, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHREPLACEMENT_FROM), .Width = 220},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHREPLACEMENT_TO), .Width = 180},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHREPLACEMENT_STOP), .Kind = GridView::CellKind::Check, .Width = 60},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&item](size_t row, size_t column) { const auto rule = item.Value()[row]; return Wide(column == 1 ? rule.From : rule.To); },
		.GetChecked = [&item](size_t row, size_t column) { const auto rule = item.Value()[row]; return column == 0 ? rule.Enabled : rule.Stop; },
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
	CreateActionRow(AddRemoveButtons(rt, item, grid, PathReplacementRule{}, 1));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddPathFilterRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Logging.PathFilters;
	auto& grid = CreateListGridRow(item, IDS_SETTINGS_PATHFILTER_DESC, 200, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHFILTER_PATTERN), .Width = 320},
		{.Title = rt.GetStringRes(IDS_SETTINGS_PATHFILTER_INCLUDE), .Kind = GridView::CellKind::Check, .Width = 60},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&item](size_t row, size_t) { return Wide(item.Value()[row].Pattern); },
		.GetChecked = [&item](size_t row, size_t column) { const auto filter = item.Value()[row]; return column == 0 ? filter.Enabled : filter.Include; },
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
	CreateActionRow(AddRemoveButtons(rt, item, grid, LogPathFilter{}, 1));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddDirectoryListRows(ConfigItem<std::vector<ToggledPath>>& item, std::vector<ToggledPath> defaults, UINT descriptionId) {
	auto& rt = m_config->Runtime;

	// An empty list shows a row of its own that lists the defaults again, if there are any.
	const auto pDefaults = std::make_shared<const std::vector<ToggledPath>>(std::move(defaults));
	const auto isPlaceholder = [&item, pDefaults] { return item.Value().empty() && !pDefaults->empty(); };
	const auto resolve = [this, &item](size_t row) { return m_config->TranslateDirectoryPath(item.Value()[row].Path); };
	const auto openRow = OpenRowInExplorer(m_hWndOwner, item, resolve);

	auto& grid = CreateListGridRow(item, descriptionId, 150, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_FOLDER), .Width = 240},
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_LOCATION), .Kind = GridView::CellKind::ReadOnly, .Width = 300},
	}, {
		.GetRowCount = [&item, isPlaceholder] { return isPlaceholder() ? 1 : item.Value().size(); },
		.GetText = [&rt, &item, isPlaceholder, resolve](size_t row, size_t column) -> std::wstring {
			if (isPlaceholder())
				return column == 1 ? std::wstring(rt.GetStringRes(IDS_SETTINGS_DIRS_ADDDEFAULT)) : std::wstring();
			if (column == 1)
				return item.Value()[row].Path.wstring();
			const auto path = resolve(row);
			std::error_code ec;
			return is_directory(path, ec) ? path.wstring() : rt.FormatStringRes(IDS_SETTINGS_DIRS_NOTFOUND, path.wstring());
		},
		// The placeholder row is past the empty list's end, which these check.
		.GetChecked = GetToggledPathEnabled(item),
		.IsEditable = [isPlaceholder](size_t, size_t column) { return column < 2 && !isPlaceholder(); },
		.SetText = SetToggledPath(item),
		.SetChecked = SetToggledPathEnabled(item),
		.MoveRow = MoveIn(item),
		.Activate = [&item, pDefaults, isPlaceholder, openRow](size_t row) {
			if (isPlaceholder())
				item = *pDefaults;
			else
				openRow(row);
		},
	});
	CreateActionRow(ToggledPathButtons(rt, m_hWndOwner, item, grid, IDS_SETTINGS_DIRS_ADD, PickKind::Folder, std::format("DirectoryList.{}", item.Name), openRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddSqpackRootRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.AdditionalSqpackRootDirectories;
	if (!m_gameDirectories) {
		m_gameDirectories = std::make_shared<GameDirectoryDetection>();
		m_gameDirectories->Owner = m_hWnd;
	}

	// Started for paths not seen yet, as the list is built or changed; reading the executables' signatures takes a while.
	const auto detect = [state = m_gameDirectories, &item] {
		for (const auto& dir : ToggledPath::EnabledOf(item.Value())) {
			auto path = Config::TranslatePath(dir);
			{
				const auto lock = std::scoped_lock(state->Mutex);
				if (state->Results.contains(path))
					continue;
				state->Results.emplace(path, std::nullopt);
			}
			std::thread([state, path = std::move(path)] {
				auto result = GameDirectoryDetection::Detect(path);
				const auto lock = std::scoped_lock(state->Mutex);
				state->Results[path] = std::move(result);
				if (state->Owner)
					PostMessageW(state->Owner, WmRefreshRows, 0, 0);
			}).detach();
		}
	};
	detect();

	const auto describe = [this, state = m_gameDirectories, &item](size_t row) -> std::wstring {
		auto& rt = m_config->Runtime;
		const auto dir = item.Value()[row];
		if (!dir.Enabled)
			return {};
		std::optional<GameDirectoryDetection::Result> result;
		{
			const auto lock = std::scoped_lock(state->Mutex);
			if (const auto it = state->Results.find(Config::TranslatePath(dir.Path)); it != state->Results.end())
				result = it->second;
		}
		if (!result)
			return rt.GetStringRes(IDS_SETTINGS_DIRS_DETECTING);
		if (!result->Found)
			return rt.GetStringRes(IDS_SETTINGS_DIRS_GAMENOTFOUND);
		std::wstring region;
		switch (result->Region) {
			case xivres::game_release_publisher::SquareEnix: region = rt.GetStringRes(IDS_CLIENT_INTERNATIONAL); break;
			case xivres::game_release_publisher::ShengquGames: region = rt.GetRegionNameLocalized(xivres::game_publisher::ShengquGames); break;
			case xivres::game_release_publisher::ActozSoft: region = rt.GetRegionNameLocalized(xivres::game_publisher::ActozSoft); break;
			case xivres::game_release_publisher::UserjoyGames: region = rt.GetRegionNameLocalized(xivres::game_publisher::UserjoyGames); break;
			default: region = result->CountryCode.empty() ? rt.GetRegionNameLocalized(xivres::game_publisher::Unspecified) : Wide(result->CountryCode); break;
		}
		return std::format(L"{} · {}", region, Wide(result->Version));
	};

	const auto resolve = [&item](size_t row) { return Config::TranslatePath(item.Value()[row].Path); };
	const auto openRow = OpenRowInExplorer(m_hWndOwner, item, resolve);
	auto& grid = CreateListGridRow(item, IDS_SETTINGS_SQPACKROOTS_DESC, 150, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_FOLDER), .Width = 240},
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_GAME), .Kind = GridView::CellKind::ReadOnly, .Width = 180},
		{.Title = rt.GetStringRes(IDS_SETTINGS_DIRS_LOCATION), .Kind = GridView::CellKind::ReadOnly, .Width = 240},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&rt, &item, resolve, describe](size_t row, size_t column) -> std::wstring {
			if (column == 1)
				return item.Value()[row].Path.wstring();
			if (column == 2)
				return describe(row);
			const auto path = resolve(row);
			std::error_code ec;
			return is_directory(path, ec) ? path.wstring() : rt.FormatStringRes(IDS_SETTINGS_DIRS_NOTFOUND, path.wstring());
		},
		.GetChecked = GetToggledPathEnabled(item),
		.IsEditable = [](size_t, size_t column) { return column < 2; },
		.SetText = SetToggledPath(item),
		.SetChecked = SetToggledPathEnabled(item),
		.MoveRow = MoveIn(item),
		.Activate = openRow,
	});
	CreateActionRow(ToggledPathButtons(rt, m_hWndOwner, item, grid, IDS_SETTINGS_DIRS_ADD, PickKind::Folder, std::format("DirectoryList.{}", item.Name), openRow));
	// Besides the refresh CreateListGridRow sets up, paths added to the list are detected.
	m_rowCleanup += item.OnChange(detect);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTextTransformationRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.TextTransformationFiles;

	// Read again each time the page is built, so that edits to the files show.
	m_textTransformationInfo.clear();
	const auto info = [this, &item](size_t row) -> const TextTransformationInfo& {
		const auto path = Config::TranslatePath(item.Value()[row].Path);
		if (const auto it = m_textTransformationInfo.find(path); it != m_textTransformationInfo.end())
			return it->second;
		TextTransformationInfo res;
		try {
			const auto json = Utils::ParseJsonFromFile(path);
			if (!json.is_object())
				throw std::runtime_error("not an object");
			if (const auto it = json.find("name"); it != json.end() && it->is_string())
				res.Name = Wide(it->get<std::string>());
			if (const auto it = json.find("description"); it != json.end() && it->is_string())
				res.Description = Wide(it->get<std::string>());
		} catch (const std::exception& e) {
			res.Description = m_config->Runtime.FormatStringRes(IDS_SETTINGS_EXCEL_PARSEERROR, Wide(e.what()));
		}
		return m_textTransformationInfo.emplace(path, std::move(res)).first->second;
	};
	const auto openRow = OpenRowInExplorer(m_hWndOwner, item, [&item](size_t row) { return Config::TranslatePath(item.Value()[row].Path); });

	auto& grid = CreateListGridRow(item, IDS_SETTINGS_EXCEL_OVERRIDEFILES_DESC, 180, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Kind = GridView::CellKind::ReadOnly, .Width = 200},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_DESCRIPTION), .Kind = GridView::CellKind::ReadOnly, .Width = 320},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_FILE), .Width = 200},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&item, info](size_t row, size_t column) -> std::wstring {
			const auto path = item.Value()[row].Path;
			if (column == 3)
				return path.wstring();
			const auto& file = info(row);
			if (column == 2)
				return file.Description;
			return file.Name.empty() ? path.filename().wstring() : file.Name;
		},
		.GetChecked = GetToggledPathEnabled(item),
		.IsEditable = [](size_t, size_t column) { return column == 0 || column == 3; },
		.SetText = SetToggledPath(item),
		.SetChecked = SetToggledPathEnabled(item),
		.MoveRow = MoveIn(item),
		.Activate = openRow,
		.ShowContextMenu = [this, &item, openRow](size_t row, POINT ptScreen) {
			if (row >= item.Value().size())
				return;
			const auto path = Config::TranslatePath(item.Value()[row].Path);
			const auto& rt = m_config->Runtime;
			ShowPopupMenu(m_hWnd, ptScreen, {
				{rt.GetStringRes(IDS_SETTINGS_EDITFILE), [&] { EditFile(m_hWndOwner, path); }},
				{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [&] { openRow(row); }, true, true},
			});
		},
	});
	CreateActionRow(ToggledPathButtons(rt, m_hWndOwner, item, grid, IDS_SETTINGS_EXCEL_ADDFILE, PickKind::Json, std::format("PathList.{}", item.Name), openRow));
	CreateNoteRow(rt.GetStringRes(IDS_SETTINGS_EXCEL_ORDER_DESC));
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
	// Under the TexTools ModPacks node (the page of those in no folder): the nested folders and packs, in the library's order.
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
		// Where a rename or move here put it; else what was shown, if still there; else a ModPack's folder; else the packs in no folder.
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

	// The same folders and packs in the same order: refresh what they show; else build the page anew, keeping the filter.
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
	const ActionButtons importAndRefresh{{menuText(ID_MODDING_TTMP_IMPORT), command(ID_MODDING_TTMP_IMPORT)}, {menuText(ID_MODDING_TTMP_REFRESH), command(ID_MODDING_TTMP_REFRESH)}};

	m_ttmpPagePacks.clear();
	AddTtmpProfileRow();
	const auto& sqpacks = m_app.GetResourceOverrider().GetVirtualSqPacks();
	if (!sqpacks) {
		CreateActionRow(importAndRefresh);
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

	// The top level has neither an enable switch nor a name.
	const auto pFolder = page->Folder;
	if (folder.empty()) {
		CreateActionRow(importAndRefresh);
	} else {
		CreateEnabledRow(
			[pFolder] { return pFolder->Enabled; },
			[this, pFolder] { SetTtmpEnabled(*pFolder, !pFolder->Enabled); },
			{
				{rt.GetStringRes(IDS_SETTINGS_TTMP_RENAME), [this] { RenameShownTtmp(); }},
				{rt.GetStringRes(IDS_SETTINGS_TTMP_MOVETO), [this, pFolder] { ShowTtmpMoveMenu({pFolder}); }},
				{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [this, pFolder] { OpenInExplorer(m_hWndOwner, pFolder->Path); }},
				{{}, {}},
				importAndRefresh[0],
				importAndRefresh[1],
			});
	}

	if (page->Items.empty()) {
		CreateNoteRow(menuText(ID_MODDING_TTMP_NOENTRY));
		return;
	}

	CreateNumberRow(rt.GetStringRes(IDS_SETTINGS_TTMP_FILTER), {}, {});
	auto& filterRow = *m_rows.back();
	filterRow.Refresh = nullptr;
	filterRow.Commit = [this, &filterRow] {
		m_ttmpFilter = GetWindowString(filterRow.Control);
		FilterTtmps();
	};
	// Before CommitOnChange: setting the text notifies a change at once.
	SetWindowTextW(filterRow.Control, m_ttmpFilter.c_str());
	filterRow.CommitOnChange = true;

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
			// Unfiltered only: a filtered order doesn't tell where they go among the rest.
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
			// Posted, so the grid is done with the click before the page it is on goes.
			m_ttmpPackToSelect = packAt(row).Path;
			PostMessageW(m_hWnd, WmSelectTtmpPack, 0, 0);
		},
		.ShowContextMenu = [this, selected](size_t, POINT ptScreen) {
			const auto packs = selected();
			if (packs.empty())
				return;
			const auto single = packs.size() == 1;
			const auto onlyPacks = std::ranges::all_of(packs, [](const auto& item) { return item->Ttmp.has_value(); });
			const auto& rt = m_config->Runtime;
			ShowPopupMenu(m_hWnd, ptScreen, {
				{rt.GetStringRes(IDS_SETTINGS_TTMP_RENAME), [&] { RenameTtmpInTree(packs.front()->Path); }, single},
				{rt.GetStringRes(IDS_SETTINGS_TTMP_MOVETO), [&] { ShowTtmpMoveMenu(packs); }},
				{rt.GetStringRes(IDS_SETTINGS_TTMP_MOVETONEWFOLDER), [&] { MoveTtmpToNewFolder(packs); }},
				{},
				{rt.GetStringRes(IDS_SETTINGS_PATCHCODE_DELETE), [&] { DeleteTtmps(packs); }, onlyPacks},
				{rt.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [&] { OpenInExplorer(m_hWndOwner, packs.front()->Path); }, single},
			});
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
	// Selected as the library picks it: the active one, or else the first; shown only with more than one.
	auto& item = m_config->Runtime.Modding.Ttmp.ChoicesFiles;
	const auto profiles = item.Value();
	PostOnChange(item, WmRebuildPage);
	if (profiles.size() < 2)
		return;

	std::vector<std::wstring> names;
	for (const auto& profile : profiles)
		names.push_back(profile.Name == ChoicesProfile::DefaultName ? std::wstring(m_config->Runtime.GetStringRes(IDS_SETTINGS_CHOICES_DEFAULTNAME)) : Wide(profile.Name.empty() ? profile.FileName : profile.Name));
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

	// Each searched directory's top level, then the nested folders; where a single item already is, is grayed.
	std::vector<PopupMenuItem> menu;
	const auto parent = items.front()->Path.parent_path();
	const auto add = [&](const std::filesystem::path& dir, const std::wstring& label) {
		menu.push_back({label, [this, &sqpacks, &items, dir] {
			RunTtmpOperation([&] { sqpacks->MoveTtmps(items, dir); }, items.size() == 1 ? dir / items.front()->Path.filename() : std::filesystem::path());
		}, items.size() != 1 || dir != parent});
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
		if (!menu.empty())
			menu.emplace_back();
		addFolders(*sqpacks->GetTtmps(), 0);
	}

	POINT pt{};
	GetCursorPos(&pt);
	ShowPopupMenu(m_hWnd, pt, menu);
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
	// Posted, so the list is done with the key or the menu before its page goes for the pack's.
	m_ttmpPackToSelect = path;
	m_ttmpRenameAfterSelect = true;
	PostMessageW(m_hWnd, WmSelectTtmpPack, 0, 0);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTtmpDetailRows(const std::shared_ptr<Features::Modding::NestedTtmp>& pack) {
	const auto& set = *pack->Ttmp;
	const auto& list = set.List;
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
	// Every language in the order used: those not listed come after the listed, so moving one lists them all.
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Languages.FallbackPriority;
	CreateListGridRow(item, IDS_SETTINGS_FALLBACK_DESC, 190, {
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
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddForcedLanguageRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Languages.ForcedCharacterLanguages;
	auto& grid = CreateListGridRow(item, IDS_SETTINGS_FORCEDLANGUAGE_DESC, 180, {
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
			const auto entry = item.Value()[row];
			if (column == 1)
				return entry.Name.empty() ? std::wstring(rt.GetStringRes(IDS_SETTINGS_FORCEDLANGUAGE_UNNAMED)) : Wide(entry.Name);
			return Wide(entry.Language);
		},
		.GetEditText = [&item](size_t row, size_t column) { const auto entry = item.Value()[row]; return Wide(column == 1 ? entry.Name : entry.Language); },
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
	CreateActionRow(AddRemoveButtons(rt, item, grid, ForcedCharacterLanguage{}, 1));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddChoicesFileRows() {
	auto& rt = m_config->Runtime;
	auto& item = rt.Modding.Ttmp.ChoicesFiles;
	auto& grid = CreateListGridRow(item, IDS_SETTINGS_CHOICES_DESC, 160, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_CHOICES_ACTIVE), .Kind = GridView::CellKind::Check, .Width = 70},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Width = 180},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_FILENAME), .Width = 180},
	}, {
		.GetRowCount = [&item] { return item.Value().size(); },
		.GetText = [&rt, &item](size_t row, size_t column) {
			const auto profile = item.Value()[row];
			if (column == 1 && profile.Name == ChoicesProfile::DefaultName)
				return std::wstring(rt.GetStringRes(IDS_SETTINGS_CHOICES_DEFAULTNAME));
			return Wide(column == 1 ? profile.Name : profile.FileName);
		},
		.GetEditText = [&item](size_t row, size_t column) { const auto profile = item.Value()[row]; return Wide(column == 1 ? profile.Name : profile.FileName); },
		.GetChecked = [&item](size_t row, size_t) { return item.Value()[row].Active; },
		.SetText = [&item](size_t row, size_t column, const std::wstring& text) {
			const auto value = Utf8(xivres::util::trim(text));
			if (column == 2 && value.find_first_of("\\/:*?\"<>|") != std::string::npos)
				return false;
			EditAt(item, row, [&](ChoicesProfile& profile) { (column == 1 ? profile.Name : profile.FileName) = value; });
			return true;
		},
		.SetChecked = [&item](size_t row, size_t, bool checked) {
			auto profiles = item.Value();
			for (size_t i = 0; i < profiles.size(); ++i)
				profiles[i].Active = i == row ? checked : false;
			item = std::move(profiles);
		},
		.MoveRow = MoveIn(item),
	});
	CreateActionRow(AddRemoveButtons(rt, item, grid, ChoicesProfile{.Active = false}, 1));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddEdgeRows() {
	auto& rt = m_config->Runtime;
	auto& edge = rt.FontReplacement.Edge;
	CreateChoiceRow(GetItemLabel(edge), true, [&edge] { return edge.Value().Enabled; }, [&edge] {
		auto config = edge.Value();
		config.Enabled = !config.Enabled;
		edge = config;
	}, true);
	m_rows.back()->Bold = true;
	SendMessageW(m_rows.back()->Control, WM_SETFONT, reinterpret_cast<WPARAM>(*m_boldFont), FALSE);
	const auto firstSlider = m_rows.size();

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
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_EDGE_SOFTNESS), 0, 8, 0.05, [&edge] { return static_cast<double>(edge.Value().Softness); }, setter(&FontReplacementEdgeConfig::Softness));
	AddHelp(m_rows.size() - 1, IDS_SETTINGS_EDGE_SOFTNESS_DESC);

	// Set while customized; the game's own edge is drawn otherwise.
	for (auto i = firstSlider; i < m_rows.size(); i++) {
		auto& row = *m_rows[i];
		row.Refresh = [&row, &edge, refresh = std::move(row.Refresh)] {
			const auto enabled = edge.Value().Enabled;
			EnableWindow(row.Control, enabled);
			EnableWindow(row.Secondary, enabled);
			refresh();
		};
	}
	PostOnChange(edge, WmRefreshRows);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddSubpixelRows() {
	auto& rt = m_config->Runtime;
	auto& clearType = rt.FontReplacement.ClearType;
	auto& mode = rt.FontReplacement.Subpixel;
	auto& vertical = rt.FontReplacement.VerticalSubpixel;

	// System takes every value from Windows, which are shown then; the stripe orders take the ones set here.
	const auto own = [&mode] { return mode.Value() != FontReplacementSubpixelMode::Off && mode.Value() != FontReplacementSubpixelMode::System; };
	const auto isVertical = [&mode] { return mode.Value() == FontReplacementSubpixelMode::VerticalRgb || mode.Value() == FontReplacementSubpixelMode::VerticalBgr; };
	const auto windows = [this] {
		const auto settings = FontReplacement::SubpixelSettings::OfWindows(m_app.GetGameWindowHandle());
		return FontReplacementClearTypeConfig{settings.Level, settings.Contrast, settings.Gamma};
	};
	const auto shown = [&clearType, windows, own] { return own() ? clearType.Value() : windows(); };

	CreateChoiceRow(GetItemLabel(clearType), true, [&mode] { return mode.Value() != FontReplacementSubpixelMode::Off; }, [&mode] {
		mode = mode.Value() == FontReplacementSubpixelMode::Off ? FontReplacementSubpixelMode::System : FontReplacementSubpixelMode::Off;
	}, true);
	m_rows.back()->Bold = true;
	SendMessageW(m_rows.back()->Control, WM_SETFONT, reinterpret_cast<WPARAM>(*m_boldFont), FALSE);

	static constexpr std::array layouts{
		FontReplacementSubpixelMode::System,
		FontReplacementSubpixelMode::Rgb,
		FontReplacementSubpixelMode::Bgr,
		FontReplacementSubpixelMode::VerticalRgb,
		FontReplacementSubpixelMode::VerticalBgr,
	};
	CreateComboRow(rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_LAYOUT), {
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_SYSTEM),
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_RGB),
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_BGR),
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_VRGB),
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_VBGR),
	}, [&mode] {
		const auto it = std::ranges::find(layouts, mode.Value());
		return it == layouts.end() ? 0 : static_cast<int>(it - layouts.begin());
	}, [&mode, &clearType, windows](int choice) {
		// The first choice of a stripe order starts from Windows' values.
		if (layouts[choice] != FontReplacementSubpixelMode::System && clearType.Value() == FontReplacementClearTypeConfig{})
			clearType = windows();
		mode = layouts[choice];
	});
	AddHelp(m_rows.size() - 1, IDS_SETTINGS_SUBPIXEL_DESC);
	const auto firstRow = m_rows.size() - 1;

	static constexpr std::array renderers{FontReplacementVerticalSubpixelRenderer::FreeType, FontReplacementVerticalSubpixelRenderer::DirectWrite};
	CreateComboRow(GetItemLabel(vertical), {
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_VERTICAL_FREETYPE),
		rt.GetStringRes(IDS_SETTINGS_SUBPIXEL_VERTICAL_DIRECTWRITE),
	}, [&vertical] {
		const auto it = std::ranges::find(renderers, vertical.Value());
		return it == renderers.end() ? 0 : static_cast<int>(it - renderers.begin());
	}, [&vertical](int choice) {
		vertical = renderers[choice];
	});
	AddHelp(m_rows.size() - 1, IDS_SETTINGS_SUBPIXEL_VERTICAL_TIP);
	const auto rendererRow = m_rows.size() - 1;

	const auto setter = [&clearType](float FontReplacementClearTypeConfig::* field) {
		return [&clearType, field](double value) {
			auto config = clearType.Value();
			config.*field = static_cast<float>(value);
			clearType = config;
		};
	};
	// As FontReplacementClearTypeConfig::Clamped has them.
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_CLEARTYPE_LEVEL), 0, 1, 0.01, [shown] { return static_cast<double>(shown().Level); }, setter(&FontReplacementClearTypeConfig::Level));
	AddHelp(m_rows.size() - 1, IDS_SETTINGS_CLEARTYPE_LEVEL_TIP);
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_CLEARTYPE_CONTRAST), 0, 4, 0.05, [shown] { return static_cast<double>(shown().Contrast); }, setter(&FontReplacementClearTypeConfig::Contrast));
	AddHelp(m_rows.size() - 1, IDS_SETTINGS_CLEARTYPE_CONTRAST_TIP);
	CreateSliderRow(rt.GetStringRes(IDS_SETTINGS_CLEARTYPE_GAMMA), 1, 2.2, 0.05, [shown] { return static_cast<double>(shown().Gamma); }, setter(&FontReplacementClearTypeConfig::Gamma));
	AddHelp(m_rows.size() - 1, IDS_SETTINGS_CLEARTYPE_GAMMA_TIP);

	// The layout can be chosen while subpixel rendering is on, what draws vertical stripes with one of them, and the values set with
	// a stripe order; refreshed in place, as rebuilding the page would scroll it to the top.
	for (auto i = firstRow; i < m_rows.size(); i++) {
		auto& row = *m_rows[i];
		row.Refresh = [&row, &mode, own, isVertical, refresh = std::move(row.Refresh), layout = i == firstRow, renderer = i == rendererRow] {
			const auto enabled = layout ? mode.Value() != FontReplacementSubpixelMode::Off : renderer ? isVertical() : own();
			EnableWindow(row.Control, enabled);
			if (row.Secondary)
				EnableWindow(row.Secondary, enabled);
			refresh();
		};
	}
	PostOnChange(mode, WmRefreshRows);
	PostOnChange(clearType, WmRefreshRows);
	PostOnChange(vertical, WmRefreshRows);
}

const std::filesystem::path& XivAlexander::Apps::MainApp::Window::SettingsView::GetFontPresetFolder() {
	if (m_fontPresetFolder.empty())
		m_fontPresetFolder = FontReplacement::Presets::Folder(*m_config);
	return m_fontPresetFolder;
}

std::vector<std::wstring> XivAlexander::Apps::MainApp::Window::SettingsView::ListPresets() {
	// Relative to the presets folder, as the sources keep them; skips the font.json of glyph image folders imported with presets.
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

namespace {
	bool LessIgnoreCase(std::wstring_view l, std::wstring_view r) {
		return CompareStringOrdinal(l.data(), static_cast<int>(l.size()), r.data(), static_cast<int>(r.size()), TRUE) == CSTR_LESS_THAN;
	}

	std::wstring_view LanguageSubtag(std::wstring_view locale) {
		return locale.substr(0, locale.find(L'-'));
	}

	// For Chinese: 0 in the simplified script, 1 in the traditional; -1 if neither is told, or not Chinese.
	int ChineseScript(std::wstring_view locale) {
		if (!Utils::EqualsI(LanguageSubtag(locale), L"zh"))
			return -1;
		for (size_t pos = locale.find(L'-'); pos != std::wstring_view::npos;) {
			const auto next = locale.find(L'-', pos + 1);
			const auto subtag = locale.substr(pos + 1, next == std::wstring_view::npos ? std::wstring_view::npos : next - pos - 1);
			if (Utils::EqualsI(subtag, L"Hans") || Utils::EqualsI(subtag, L"CN") || Utils::EqualsI(subtag, L"SG"))
				return 0;
			if (Utils::EqualsI(subtag, L"Hant") || Utils::EqualsI(subtag, L"TW") || Utils::EqualsI(subtag, L"HK") || Utils::EqualsI(subtag, L"MO"))
				return 1;
			pos = next;
		}
		return -1;
	}

	std::wstring GetLocalizedString(IDWriteLocalizedStrings* strings, UINT32 index) {
		UINT32 length = 0;
		if (FAILED(strings->GetStringLength(index, &length)))
			return {};
		std::wstring res(static_cast<size_t>(length) + 1, L'\0');
		if (FAILED(strings->GetString(index, res.data(), length + 1)))
			return {};
		res.resize(length);
		return res;
	}

	// Of the first preference it has: exactly, else the neutral language, else the same language (never across the Chinese scripts); else English; else the first.
	std::wstring GetPreferredLocalizedString(IDWriteLocalizedStrings* strings, std::span<const std::wstring> preferences) {
		const auto count = strings->GetCount();
		if (!count)
			return {};

		std::vector<std::wstring> locales(count);
		for (UINT32 i = 0; i < count; ++i) {
			UINT32 length = 0;
			if (FAILED(strings->GetLocaleNameLength(i, &length)))
				continue;
			locales[i].resize(static_cast<size_t>(length) + 1);
			if (FAILED(strings->GetLocaleName(i, locales[i].data(), length + 1)))
				locales[i].clear();
			else
				locales[i].resize(length);
		}

		const auto pick = [&]() -> UINT32 {
			for (const auto& preference : preferences) {
				UINT32 index = 0;
				BOOL exists = FALSE;
				if (SUCCEEDED(strings->FindLocaleName(preference.c_str(), &index, &exists)) && exists)
					return index;

				const auto language = LanguageSubtag(preference);
				const auto script = ChineseScript(preference);
				const auto sameScript = [script](std::wstring_view locale) {
					const auto other = ChineseScript(locale);
					return script < 0 || other < 0 || script == other;
				};
				for (UINT32 i = 0; i < count; ++i) {
					if (Utils::EqualsI(locales[i], language))
						return i;
				}
				for (UINT32 i = 0; i < count; ++i) {
					if (Utils::EqualsI(LanguageSubtag(locales[i]), language) && sameScript(locales[i]))
						return i;
				}
			}
			for (UINT32 i = 0; i < count; ++i) {
				if (Utils::EqualsI(LanguageSubtag(locales[i]), L"en"))
					return i;
			}
			return 0;
		};
		return GetLocalizedString(strings, pick());
	}

	const std::wstring EnglishLocale[]{L"en-US"};
}

const std::vector<XivAlexander::Apps::MainApp::Window::SettingsView::SystemFontFamily>& XivAlexander::Apps::MainApp::Window::SettingsView::ListSystemFontFamilies() {
	// Read again only when the UI's language changes.
	const auto language = static_cast<int>(m_config->Runtime.Ui.Language.Value());
	if (language == m_systemFontFamiliesLanguage)
		return m_systemFontFamilies;
	m_systemFontFamiliesLanguage = language;
	m_systemFontFamilies.clear();

	const auto preferences = m_config->Runtime.GetUiLocalePreferences();
	IDWriteFactoryPtr factory;
	IDWriteFontCollectionPtr collection;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)))
		|| FAILED(factory->GetSystemFontCollection(&collection)))
		return m_systemFontFamilies;

	auto& families = m_systemFontFamilies;
	for (UINT32 i = 0, count = collection->GetFontFamilyCount(); i < count; ++i) {
		IDWriteFontFamilyPtr family;
		IDWriteLocalizedStringsPtr names;
		if (FAILED(collection->GetFontFamily(i, &family)) || FAILED(family->GetFamilyNames(&names)))
			continue;
		auto key = GetPreferredLocalizedString(names, EnglishLocale);
		if (key.empty())
			continue;
		auto display = GetPreferredLocalizedString(names, preferences);
		if (display.empty())
			display = key;
		families.push_back({.Key = std::move(key), .Display = std::move(display)});
	}

	std::ranges::sort(families, [](const auto& l, const auto& r) { return LessIgnoreCase(l.Key, r.Key); });
	const auto [eraseFrom, eraseTo] = std::ranges::unique(families, [](const auto& l, const auto& r) { return Utils::EqualsI(l.Key, r.Key); });
	families.erase(eraseFrom, eraseTo);

	// A name shown for more than one gets its key, except where it is the key.
	std::ranges::stable_sort(families, [](const auto& l, const auto& r) { return LessIgnoreCase(l.Display, r.Display); });
	for (size_t i = 0; i < families.size();) {
		auto j = i + 1;
		while (j < families.size() && Utils::EqualsI(families[i].Display, families[j].Display))
			++j;
		if (j - i > 1) {
			for (auto k = i; k < j; ++k) {
				if (!Utils::EqualsI(families[k].Display, families[k].Key))
					families[k].Display = std::format(L"{} ({})", families[k].Display, families[k].Key);
			}
		}
		i = j;
	}

	// As the UI's language sorts them.
	const auto sortLocale = preferences.empty() ? LOCALE_NAME_USER_DEFAULT : preferences.front().c_str();
	std::ranges::sort(families, [sortLocale](const SystemFontFamily& l, const SystemFontFamily& r) {
		const auto order = CompareStringEx(sortLocale, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
			l.Display.c_str(), static_cast<int>(l.Display.size()), r.Display.c_str(), static_cast<int>(r.Display.size()), nullptr, nullptr, 0);
		if (order == CSTR_LESS_THAN || order == CSTR_GREATER_THAN)
			return order == CSTR_LESS_THAN;
		if (!order && !Utils::EqualsI(l.Display, r.Display))
			return LessIgnoreCase(l.Display, r.Display);
		return LessIgnoreCase(l.Key, r.Key);
	});
	return m_systemFontFamilies;
}

std::wstring XivAlexander::Apps::MainApp::Window::SettingsView::SystemFontDisplayName(const std::string& key) {
	const auto wideKey = Wide(key);
	const auto& families = ListSystemFontFamilies();
	if (const auto it = std::ranges::find_if(families, [&](const auto& f) { return Utils::EqualsI(f.Key, wideKey); }); it != families.end())
		return it->Display;
	if (const auto it = std::ranges::find_if(families, [&](const auto& f) { return Utils::EqualsI(f.Display, wideKey); }); it != families.end())
		return it->Display;
	return wideKey;
}

std::wstring XivAlexander::Apps::MainApp::Window::SettingsView::SystemFontKey(const std::wstring& typed) {
	const auto name = Trim(typed);
	const auto& families = ListSystemFontFamilies();
	if (const auto it = std::ranges::find_if(families, [&](const auto& f) { return Utils::EqualsI(f.Display, name); }); it != families.end())
		return it->Key;
	if (const auto it = std::ranges::find_if(families, [&](const auto& f) { return Utils::EqualsI(f.Key, name); }); it != families.end())
		return it->Key;

	// By any of its names, such as one in another language.
	IDWriteFactoryPtr factory;
	IDWriteFontCollectionPtr collection;
	UINT32 index = 0;
	BOOL exists = FALSE;
	IDWriteFontFamilyPtr family;
	IDWriteLocalizedStringsPtr names;
	if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&factory)))
		&& SUCCEEDED(factory->GetSystemFontCollection(&collection))
		&& SUCCEEDED(collection->FindFamilyName(name.c_str(), &index, &exists)) && exists
		&& SUCCEEDED(collection->GetFontFamily(index, &family))
		&& SUCCEEDED(family->GetFamilyNames(&names))) {
		if (auto key = GetPreferredLocalizedString(names, EnglishLocale); !key.empty())
			return key;
	}
	return name;
}

std::optional<std::string> XivAlexander::Apps::MainApp::Window::SettingsView::ChooseFontPreset() {
	auto& rt = m_config->Runtime;
	const auto& folder = GetFontPresetFolder();
	const auto picked = PickPath(m_hWndOwner, folder, PickKind::Preset, "FontPreset");
	if (!picked)
		return std::nullopt;

	// Canonical first, so a path through a link or in another case is still found to be in the presets folder.
	std::error_code ec;
	auto canonicalFolder = weakly_canonical(folder, ec);
	if (ec)
		canonicalFolder = absolute(folder).lexically_normal();
	auto canonicalPicked = weakly_canonical(*picked, ec);
	if (ec)
		canonicalPicked = absolute(*picked).lexically_normal();
	if (const auto relative = canonicalPicked.lexically_relative(canonicalFolder); !relative.empty() && *relative.begin() != L"..")
		return Utf8(relative.wstring());

	// Glyph image folders it reads are copied along only if under its folder; if any isn't, it can only be used where it is.
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
			return std::nullopt;
	}
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddFontFamilyRows(const std::string& family) {
	auto& rt = m_config->Runtime;
	auto& item = rt.FontReplacement.Faces.Families;

	// A family as the defaults have it (on, without sources) is left out.
	const auto getFamily = [&item, family]() -> FontReplacementFamily {
		const auto all = item.Snapshot();
		const auto it = all->find(family);
		return it == all->end() ? FontReplacementFamily{} : it->second;
	};
	const auto setFamily = [&item, family](FontReplacementFamily settings) {
		auto all = item.Value();
		if (settings == FontReplacementFamily{})
			all.erase(family);
		else
			all[family] = std::move(settings);
		item = std::move(all);
	};
	// Later sources take priority; the rows list them from the top priority.
	const auto get = [getFamily] {
		auto sources = getFamily().Sources;
		std::ranges::reverse(sources);
		return sources;
	};
	const auto set = [getFamily, setFamily](std::vector<FontReplacementFamilySource> sources) {
		std::ranges::reverse(sources);
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
		sources.insert(sources.begin(), std::move(source));
		set(std::move(sources));
		auto& grid = **pGrid;
		grid.Refresh();
		if (editName)
			grid.BeginEdit(0, 2);
		else
			grid.Select(0);
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
			{rt.GetStringRes(IDS_SETTINGS_FONT_REMOVEALL), [this, family, get, set, pGrid] {
				if (get().empty())
					return;
				if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.FormatStringRes(IDS_SETTINGS_FONT_CONFIRMREMOVEALL, Wide(family))) != IDYES)
					return;
				set({});
				(*pGrid)->Refresh();
			}},
			{{}, {}},
			{rt.GetStringRes(IDS_SETTINGS_FONT_COPYTOOTHERS), [this, &item, family, getFamily] {
				if (Dll::MessageBoxF(m_hWndOwner, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, m_config->Runtime.FormatStringRes(IDS_SETTINGS_FONT_CONFIRMCOPY, Wide(family))) != IDYES)
					return;
				// Only the sources, with their digits; whether the others are on stays as it is.
				auto all = item.Value();
				const auto sources = getFamily().Sources;
				for (const auto& other : m_fontFamilies) {
					if (other == family)
						continue;
					auto& settings = all[other];
					settings.Sources = sources;
					if (settings == FontReplacementFamily{})
						all.erase(other);
				}
				item = std::move(all);
			}},
			{{}, {}},
			{rt.GetStringRes(IDS_SETTINGS_FONT_OPENPRESETFOLDER), [this] { OpenInExplorer(m_hWndOwner, GetFontPresetFolder()); }},
		});

	const auto cellText = [this, &rt, get](size_t row, size_t column, bool editing) -> std::wstring {
		const auto sources = get();
		const auto& source = sources[row];
		switch (column) {
			case 1:
				return rt.GetStringRes(source.IsPreset() ? IDS_SETTINGS_FONT_SOURCE_PRESET : IDS_SETTINGS_FONT_SOURCE_FONT);
			case 2:
				if (source.IsPreset())
					return editing ? Wide(source.Preset) : std::filesystem::path(Wide(source.Preset)).filename().wstring();
				return SystemFontDisplayName(source.Font.Name);
			case 3:
				return source.IsPreset() ? std::wstring() : NamedValue(rt, FontWeights, source.Font.Weight);
			case 4:
				return source.IsPreset() ? std::wstring() : NamedValue(rt, FontStretches, source.Font.Stretch);
			case 5:
				return source.IsPreset() ? std::wstring() : NamedValue(rt, FontStyles, source.Font.Style);
			default:
				return {};
		}
	};

	auto& grid = CreateGridRow({}, 0, 200, {
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_ENABLED), .Kind = GridView::CellKind::Check, .Width = 60},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_SOURCE_TYPE), .Kind = GridView::CellKind::ReadOnly, .Width = 100},
		{.Title = rt.GetStringRes(IDS_SETTINGS_GRID_NAME), .Kind = GridView::CellKind::ComboEdit, .Width = 220, .GetChoices = [this, get](size_t row) {
			if (get()[row].IsPreset())
				return ListPresets();
			std::vector<std::wstring> names;
			for (const auto& family : ListSystemFontFamilies())
				names.push_back(family.Display);
			return names;
		}},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_WEIGHT), .Kind = GridView::CellKind::ComboChoice, .Width = 120, .GetChoices = [&rt](size_t) { return NamedValues(rt, FontWeights); }},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_STRETCH), .Kind = GridView::CellKind::ComboChoice, .Width = 130, .GetChoices = [&rt](size_t) { return NamedValues(rt, FontStretches); }},
		{.Title = rt.GetStringRes(IDS_SETTINGS_FONT_STYLE), .Kind = GridView::CellKind::ComboChoice, .Width = 90, .GetChoices = [&rt](size_t) { return NamedValues(rt, FontStyles); }},
	}, {
		.GetRowCount = [get] { return get().size(); },
		.GetText = [cellText](size_t row, size_t column) { return cellText(row, column, false); },
		.GetEditText = [cellText](size_t row, size_t column) { return cellText(row, column, true); },
		.GetChecked = [get](size_t row, size_t) { return get()[row].Enabled; },
		.IsEditable = [get](size_t row, size_t column) { return column <= 2 || !get()[row].IsPreset(); },
		.SetText = [this, edit, parseNumber](size_t row, size_t column, const std::wstring& text) {
			const auto value = xivres::util::trim(text);
			if (column == 2 && value.empty())
				return false;
			edit(row, [&](FontReplacementFamilySource& source) {
				switch (column) {
					case 2:
						// A font is kept by its English name; one shown unchanged is left as saved.
						if (source.IsPreset())
							source.Preset = Utf8(value);
						else if (value != SystemFontDisplayName(source.Font.Name))
							source.Font.Name = Utf8(SystemFontKey(value));
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
		.ShowContextMenu = [this, get, edit, remove](size_t row, POINT ptScreen) {
			if (row >= get().size())
				return;
			const auto source = get()[row];
			const auto& preset = source.Preset;
			std::vector<PopupMenuItem> items{
				{m_config->Runtime.GetStringRes(IDS_SETTINGS_REMOVE), remove},
				{},
			};
			if (!source.IsPreset()) {
				items.push_back({m_config->Runtime.GetStringRes(IDS_SETTINGS_FONT_MONOSPACEDDIGITS), [&] {
					edit(row, [](FontReplacementFamilySource& s) { s.MonospacedDigits = !s.MonospacedDigits; });
				}, true, false, source.MonospacedDigits});
			} else {
				items.push_back({m_config->Runtime.GetStringRes(IDS_SETTINGS_EDITFILE), [&] { EditFile(m_hWndOwner, FontReplacement::Presets::Resolve(GetFontPresetFolder(), preset)); }, !preset.empty()});
				items.push_back({m_config->Runtime.GetStringRes(IDS_SETTINGS_SHOWINEXPLORER), [&] { OpenInExplorer(m_hWndOwner, FontReplacement::Presets::Resolve(GetFontPresetFolder(), preset)); }, !preset.empty()});
			}
			ShowPopupMenu(m_hWnd, ptScreen, items);
		},
		.KeyDown = [remove](UINT vk) {
			if (vk != VK_DELETE)
				return false;
			remove();
			return true;
		},
	});
	*pGrid = &grid;
	PostOnChange(item, WmRefreshRows);
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

HWND XivAlexander::Apps::MainApp::Window::SettingsView::CreateRowControl(int part, DWORD exStyle, LPCWSTR className, const std::wstring& text, DWORD style, std::optional<size_t> rowIndex) {
	const auto id = FirstRowControlId + static_cast<int>(rowIndex.value_or(m_rows.size())) * ControlsPerRow + part;
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

	// CRLF paragraphs, as the control takes them; links are notified to the page.
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
		std::vector<PopupMenuItem> items;
		for (const auto& [text, action] : actions)
			items.push_back({text, action});
		ShowPopupMenu(m_hWnd, {rc.right, rc.bottom}, items, TPM_RIGHTALIGN | TPM_TOPALIGN);
	};
	m_rows.emplace_back(std::move(pRow));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddTabbedRows() {
	// The tab chosen last for these lists is chosen again.
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
	auto pRow = std::make_unique<Row>();
	auto& row = *pRow;
	row.Type = Row::RowType::Heading;
	row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, text, SS_LEFT | SS_NOPREFIX);
	SendMessageW(row.Label, WM_SETFONT, reinterpret_cast<WPARAM>(*m_boldFont), FALSE);
	if (!checkLabel.empty()) {
		row.Control = CreateRowControl(PartControl, 0, WC_BUTTONW, EscapeMnemonics(checkLabel), WS_TABSTOP | BS_CHECKBOX | BS_NOTIFY);
		EnableWindow(row.Control, enabled);
	}
	if (link) {
		// At the line's end, drawn as a link (WM_DRAWITEM) for an action lighter than a button's.
		row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, linkLabel, WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY);
		row.LinkButton = true;
	}
	row.Refresh = [&row, isChecked = std::move(isChecked)] {
		if (row.Control)
			Button_SetCheck(row.Control, isChecked() ? BST_CHECKED : BST_UNCHECKED);
	};
	row.Click = [this, toggle = std::move(toggle), link = std::move(link)](int part) {
		if (part == PartButton1) {
			if (link)
				link();
			return;
		}
		if (!toggle)
			return;
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

void XivAlexander::Apps::MainApp::Window::SettingsView::AddDescription(size_t rowIndex, UINT descriptionId) {
	if (rowIndex >= m_rows.size() || !descriptionId || m_rows[rowIndex]->Description)
		return;
	m_rows[rowIndex]->Description = CreateRowControl(PartDescription, 0, WC_STATICW, m_config->Runtime.GetStringRes(descriptionId), SS_LEFT | SS_NOPREFIX, rowIndex);
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddHelp(size_t rowIndex, UINT tooltipId) {
	if (tooltipId)
		AddHelp(rowIndex, std::wstring(m_config->Runtime.GetStringRes(tooltipId)));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::AddHelp(size_t rowIndex, const std::wstring& text) {
	if (rowIndex >= m_rows.size() || text.empty())
		return;
	auto& row = *m_rows[rowIndex];
	if (row.Help) {
		// A second text goes after the first, as the version-sensitive mark's does.
		const auto combined = GetWindowString(row.Help) + L"\n\n" + text;
		SetWindowTextW(row.Help, combined.c_str());
		TTTOOLINFOW ti{.cbSize = sizeof ti, .hwnd = m_hPage, .uId = reinterpret_cast<UINT_PTR>(row.Help), .lpszText = const_cast<LPWSTR>(combined.c_str())};
		SendMessageW(m_hTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
		return;
	}

	// Drawn as a link (WM_DRAWITEM); after the label in the tab order, whenever the row was made.
	const auto id = FirstRowControlId + static_cast<int>(rowIndex) * ControlsPerRow + PartHelp;
	row.Help = CreateWindowExW(0, WC_BUTTONW, text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY,
		0, 0, 0, 0, m_hPage, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), Dll::Module(), nullptr);
	SendMessageW(row.Help, WM_SETFONT, reinterpret_cast<WPARAM>(*m_font), FALSE);
	ApplyThemeToControl(row.Help);
	if (const auto hBefore = row.Label ? row.Label : row.Control)
		SetWindowPos(row.Help, hBefore, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	TTTOOLINFOW ti{.cbSize = sizeof ti, .uFlags = TTF_IDISHWND | TTF_SUBCLASS, .hwnd = m_hPage, .uId = reinterpret_cast<UINT_PTR>(row.Help), .lpszText = const_cast<LPWSTR>(text.c_str())};
	SendMessageW(m_hTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ShowHelp(HWND hHelp, bool show) {
	TTTOOLINFOW ti{.cbSize = sizeof ti, .hwnd = m_hPage, .uId = TrackedHelpToolId};
	if (!show || !hHelp) {
		if (m_trackedHelp)
			SendMessageW(m_hTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
		m_trackedHelp = nullptr;
		return;
	}

	// Below the button, as a hover would show it.
	auto text = GetWindowString(hHelp);
	ti.lpszText = text.data();
	SendMessageW(m_hTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
	RECT rc{};
	GetWindowRect(hHelp, &rc);
	SendMessageW(m_hTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(rc.left, rc.bottom + 2));
	SendMessageW(m_hTooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
	m_trackedHelp = hHelp;
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
		{&rt.FontReplacement.ClearType, [this] { AddSubpixelRows(); }},
		{&rt.Modding.AdditionalSqpackRootDirectories, [this] { AddSqpackRootRows(); }},
		{&rt.Modding.TextTransformationFiles, [this] { AddTextTransformationRows(); }},
		{&rt.Modding.GameResourceFileEntryRootDirectories, [this, &rt] {
			AddDirectoryListRows(rt.Modding.GameResourceFileEntryRootDirectories, rt.Modding.DefaultGameResourceFileEntryRootDirectories(), IDS_SETTINGS_FILEENTRYROOTS_DESC);
		}},
		{&rt.Modding.Ttmp.SearchDirectories, [this, &rt] {
			AddDirectoryListRows(rt.Modding.Ttmp.SearchDirectories, rt.Modding.DefaultTtmpSearchDirectories(), IDS_SETTINGS_TTMPDIRS_DESC);
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

	const auto itemInfo = FindItemInfo(item);
	if (itemInfo && itemInfo->NoteId)
		CreateNoteRow(rt.GetStringRes(itemInfo->NoteId));

	if (const auto first = m_rows.size(); CreateSpecialRows(item)) {
		if (itemInfo)
			AddHelp(first, itemInfo->TooltipId);
		return;
	}

	if (itemInfo) {
		row.PickFiles = itemInfo->PickFiles;
		row.BesideCheck = itemInfo->BesideCheck;
	}

	if (&item == &rt.Modding.Languages.ResourceOverride || &item == &rt.Modding.Languages.VoiceResourceOverride) {
		// Unspecified leaves the game's own language.
		BindEnum<xivres::game_language>(row, [&rt, &gameLanguageChoice] {
			std::vector<std::pair<xivres::game_language, std::wstring>> res{{xivres::game_language::Unspecified, rt.GetStringRes(IDS_SETTINGS_LANGUAGEOVERRIDE_DISABLE)}};
			for (auto i = static_cast<int>(xivres::game_language::Japanese); i <= static_cast<int>(xivres::game_language::TraditionalChinese); ++i)
				res.emplace_back(gameLanguageChoice(static_cast<xivres::game_language>(i)));
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
		|| BindEnum<TalkLineCount>(row, [&rt] {
			return std::vector<std::pair<TalkLineCount, std::wstring>>{
				{TalkLineCount::Automatic, rt.GetStringRes(IDS_SETTINGS_TALKLINES_AUTOMATIC)},
				{TalkLineCount::Three, rt.GetStringRes(IDS_SETTINGS_TALKLINES_THREE)},
				{TalkLineCount::Four, rt.GetStringRes(IDS_SETTINGS_TALKLINES_FOUR)},
			};
		})
		|| BindEnum<xivres::game_publisher>(row, [&rt] {
			std::vector<std::pair<xivres::game_publisher, std::wstring>> res;
			for (auto i = 0; i <= static_cast<int>(xivres::game_publisher::UserjoyGames); ++i)
				res.emplace_back(static_cast<xivres::game_publisher>(i), rt.GetRegionNameLocalized(static_cast<xivres::game_publisher>(i)));
			return res;
		})
		|| BindEnum<FontReplacementNamePlateMode>(row, [&rt] {
			return std::vector<std::pair<FontReplacementNamePlateMode, std::wstring>>{
				{FontReplacementNamePlateMode::Game, rt.GetStringRes(IDS_SETTINGS_NAMEPLATES_GAME)},
				{FontReplacementNamePlateMode::BakedAtFullSize, rt.GetStringRes(IDS_SETTINGS_NAMEPLATES_BAKED)},
				{FontReplacementNamePlateMode::Live, rt.GetStringRes(IDS_SETTINGS_NAMEPLATES_LIVE)},
			};
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

	auto label = GetItemLabel(item);
	const auto versionMarked = itemInfo && itemInfo->VersionSensitive && rt.AreVersionSensitiveFeaturesDisabledTemporarily();
	if (versionMarked)
		label = L"(!) " + label;
	if (row.Type != Row::RowType::Check)
		row.Label = CreateRowControl(PartLabel, 0, WC_STATICW, label, SS_LEFT | SS_NOPREFIX);

	UINT descriptionId = 0;
	if (const auto info = FindItemInfo(item))
		descriptionId = info->DescriptionId;
	if (row.Type == Row::RowType::Unsupported)
		descriptionId = IDS_SETTINGS_EDITINCONFIGEDITOR;
	if (descriptionId)
		row.Description = CreateRowControl(PartDescription, 0, WC_STATICW, rt.GetStringRes(descriptionId), SS_LEFT | SS_NOPREFIX);

	constexpr DWORD ButtonStyle = WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY;
	switch (row.Type) {
		case Row::RowType::Check:
			// With its label, so that the focus is drawn around it.
			row.Control = CreateRowControl(PartControl, 0, WC_BUTTONW, EscapeMnemonics(label), WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE | BS_TOP | BS_NOTIFY);
			if (itemInfo && itemInfo->Status) {
				row.GetStatus = itemInfo->Status;
				row.Status = CreateRowControl(PartStatus, 0, WC_STATICW, row.GetStatus(), SS_LEFT | SS_NOPREFIX);
			}
			break;

		case Row::RowType::Edit:
			row.Control = CreateRowControl(PartControl, WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL);
			break;

		case Row::RowType::Combo:
			row.Control = CreateRowControl(PartControl, 0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST);
			for (const auto& choice : row.ComboLabels)
				ComboBox_AddString(row.Control, choice.c_str());
			break;

		case Row::RowType::ComboEdit:
			row.Control = CreateRowControl(PartControl, 0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL);
			for (const auto& choice : row.ComboLabels)
				ComboBox_AddString(row.Control, choice.c_str());
			break;

		case Row::RowType::PathEdit:
			row.Control = CreateRowControl(PartControl, WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL);
			row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_BROWSE), ButtonStyle);
			break;

		case Row::RowType::PathList:
			row.Control = CreateRowControl(PartControl, WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT);
			row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_ADD), ButtonStyle);
			row.Button2 = CreateRowControl(PartButton2, 0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_REMOVE), ButtonStyle);
			break;

		case Row::RowType::Unsupported:
			row.Button1 = CreateRowControl(PartButton1, 0, WC_BUTTONW, rt.GetStringRes(IDS_SETTINGS_OPENEDITOR), ButtonStyle);
			break;

		default:
			break;
	}

	// Changes from anywhere, the menus included, show here.
	PostOnChange(item, WmRefreshRows);
	m_rows.emplace_back(std::move(pRow));
	if (versionMarked)
		AddHelp(m_rows.size() - 1, IDS_SETTINGS_VERSIONSENSITIVE_TIP);
	if (itemInfo)
		AddHelp(m_rows.size() - 1, itemInfo->TooltipId);
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
		const auto text = GetWindowString(hWnd);
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

	// A "(?)" goes after its row's label, which is narrowed by its width when it has one; labels fit their text then, so as not to cover it.
	const auto textWidth = [hdc](HWND hWnd) {
		if (!hWnd)
			return 0;
		const auto text = GetWindowString(hWnd);
		RECT rc{};
		DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		return static_cast<int>(rc.right);
	};
	const auto helpWidth = [&] {
		RECT rc{};
		DrawTextW(hdc, HelpMark, -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		return static_cast<int>(rc.right) + scale(6);
	}();
	const auto helpSpace = helpWidth + scale(4);
	// Places a label of up to width, wrapped, with its "(?)" after it; returns its height.
	const auto placeLabel = [&](const Row& row, int x, int y, int width) {
		if (!row.Help) {
			const auto height = measure(row.Label, width);
			place(row.Label, x, y, width, height);
			return height;
		}
		const auto box = std::max(1, width - helpSpace);
		const auto height = measure(row.Label, box);
		const auto shown = height > lineHeight ? box : std::min(box, textWidth(row.Label) + scale(2));
		place(row.Label, x, y, shown, height);
		place(row.Help, row.Label ? x + shown + scale(4) : x, y, helpWidth, lineHeight);
		return std::max(height, lineHeight);
	};

	// Laid out with the grids at their own heights, then again with what the page has left shared among them alike.
	const auto layoutRows = [&](int gridExtra) {
		placements.clear();
		auto y = pad;
		auto flowGroup = 0;
		auto flowX = pad;
		auto lineGroup = 0;
		auto lineX = pad;
		auto besideX = -1;  // Where a number beside the check box just placed starts.
		auto lastRowY = 0;
		auto lastRowHeight = 0;
		for (const auto& row : m_rows) {
			if (row->TabOwner) {
				const auto shown = row->TabIndex == row->TabOwner->ActiveTab;
				for (const auto hWnd : {row->Label, row->Control, row->Button1, row->Button2, row->Button3, row->Button4, row->Description, row->Secondary, row->Help, row->Status}) {
					if (hWnd)
						ShowWindow(hWnd, shown ? SW_SHOWNA : SW_HIDE);
				}
				if (!shown)
					continue;
			}
			const auto previousFlowGroup = std::exchange(flowGroup, 0);
			const auto previousLineGroup = std::exchange(lineGroup, 0);
			const auto previousBesideX = std::exchange(besideX, -1);
			SelectObject(hdc, *m_font);
			int rowHeight;
			if (row->BesideCheck && row->Type == Row::RowType::Edit && previousBesideX >= 0) {
				// On the check box's line, the box taller than the line, centered on it; the label after it is the unit.
				const auto numberWidth = scale(72);
				const auto editY = lastRowY + (lastRowHeight - controlHeight) / 2;
				const auto labelX = previousBesideX + numberWidth + scale(6);
				place(row->Control, previousBesideX, editY, numberWidth, controlHeight);
				place(row->Label, labelX, lastRowY + std::max(0, (lastRowHeight - lineHeight) / 2), std::max(0, pad + inner - labelX), lineHeight);
				y = std::max(y, editY + controlHeight + rowGap);
				continue;
			}
			if (row->LineGroup) {
				// On the previous row's line if the label and a little of the control fit; the group's (or line's) last takes the rest.
				const auto next = &row + 1 != m_rows.data() + m_rows.size() ? (&row)[1].get() : nullptr;
				const auto labelText = GetWindowString(row->Label);
				RECT rcLabel{};
				DrawTextW(hdc, labelText.c_str(), static_cast<int>(labelText.size()), &rcLabel, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
				const auto labelWidth = static_cast<int>(rcLabel.right);
				const auto labelSpan = labelWidth + (row->Help ? helpSpace : 0);
				const auto wanted = scale(row->LineWidth ? row->LineWidth : 120);
				if (previousLineGroup == row->LineGroup && lineX + labelSpan + gap + wanted <= pad + inner)
					y -= controlHeight + rowGap;
				else
					lineX = pad;
				place(row->Label, lineX, y + std::max(0, (controlHeight - lineHeight) / 2), labelWidth, lineHeight);
				place(row->Help, lineX + labelWidth + scale(4), y + std::max(0, (controlHeight - lineHeight) / 2), helpWidth, lineHeight);
				const auto controlX = lineX + labelSpan + gap;
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
						auto labelWidth = inner;
						if (row->Button1) {
							RECT rcLink{};
							const auto link = GetWindowString(row->Button1);
							DrawTextW(hdc, link.c_str(), static_cast<int>(link.size()), &rcLink, DT_CALCRECT | DT_SINGLELINE);
							const auto linkWidth = static_cast<int>(rcLink.right) + scale(6);
							labelWidth = std::max(0, inner - linkWidth - gap);
							place(row->Button1, pad + inner - linkWidth, y, linkWidth, lineHeight);
						}
						SelectObject(hdc, *m_boldFont);
						rowHeight = std::max(row->Button1 ? lineHeight : 0, placeLabel(*row, pad, y, labelWidth));
						SelectObject(hdc, *m_font);
						break;
					}

					SelectObject(hdc, *m_boldFont);
					RECT rcHeading{};
					const auto heading = GetWindowString(row->Label);
					DrawTextW(hdc, heading.c_str(), static_cast<int>(heading.size()), &rcHeading, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
					SelectObject(hdc, *m_font);
					RECT rcCheck{};
					const auto check = GetWindowString(row->Control);
					DrawTextW(hdc, check.c_str(), static_cast<int>(check.size()), &rcCheck, DT_CALCRECT | DT_SINGLELINE);
					const auto headingWidth = std::min(inner, static_cast<int>(rcHeading.right) + scale(2));
					const auto checkX = pad + headingWidth + scale(16);
					const auto checkWidth2 = std::max(0, std::min(pad + inner - checkX, checkWidth + scale(4) + static_cast<int>(rcCheck.right) + scale(6)));
					rowHeight = std::max(lineHeight, static_cast<int>(rcHeading.bottom)) + 2;
					place(row->Label, pad, y + 1, headingWidth, rowHeight - 2);
					place(row->Control, checkX, y, checkWidth2, rowHeight);
					const auto helpX = checkX + checkWidth2 + scale(4);
					place(row->Help, helpX, y + 1, helpWidth, lineHeight);
					if (row->Button1) {
						RECT rcLink{};
						const auto link = GetWindowString(row->Button1);
						DrawTextW(hdc, link.c_str(), static_cast<int>(link.size()), &rcLink, DT_CALCRECT | DT_SINGLELINE);
						const auto linkWidth = static_cast<int>(rcLink.right) + scale(6);
						place(row->Button1, std::max((row->Help ? helpX + helpWidth : checkX + checkWidth2) + gap, pad + inner - linkWidth), y, linkWidth, rowHeight);
					}
					break;
				}

				case Row::RowType::Note: {
					rowHeight = measure(row->Description, inner);
					place(row->Description, pad, y, inner, rowHeight);
					break;
				}

				case Row::RowType::Tabs: {
					auto x = pad;
					auto lineY = y;
					for (const auto hButton : row->Tabs) {
						RECT rcText{};
						const auto text = GetWindowString(hButton);
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
					// Three lines, with a button for the rest if there is more; the control has a margin of its own.
					const auto full = measure(row->Description, std::max(1, inner - scale(8))) + scale(4);
					const auto collapsed = lineHeight * 3 + scale(4);
					const auto shortened = full > collapsed;
					const auto textHeight = shortened && !row->Expanded ? collapsed : full;
					place(row->Description, pad, y, inner, textHeight);
					rowHeight = textHeight;
					if (shortened) {
						RECT rcButton{};
						const auto buttonText = GetWindowString(row->Button1);
						DrawTextW(hdc, buttonText.c_str(), static_cast<int>(buttonText.size()), &rcButton, DT_CALCRECT | DT_SINGLELINE);
						place(row->Button1, pad, y + textHeight + scale(4), std::max(buttonWidth, static_cast<int>(rcButton.right) + scale(24)), controlHeight);
						rowHeight += scale(4) + controlHeight;
					} else {
						place(row->Button1, 0, 0, 0, 0);
					}
					break;
				}

				case Row::RowType::Action: {
					auto x = pad;
					auto lineY = y;
					for (const auto hButton : {row->Button1, row->Button2, row->Button3, row->Button4}) {
						if (!hButton)
							continue;
						RECT rcText{};
						const auto text = GetWindowString(hButton);
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
					place(row->Help, x, lineY + std::max(0, (controlHeight - lineHeight) / 2), helpWidth, lineHeight);
					rowHeight = lineY + controlHeight - y;
					break;
				}

				case Row::RowType::Grid: {
					auto gridY = y;
					if (const auto labelHeight = row->Label || row->Help ? placeLabel(*row, pad, gridY, inner) : 0)
						gridY += labelHeight + scale(2);
					if (const auto descriptionHeight = measure(row->Description, inner)) {
						place(row->Description, pad, gridY, inner, descriptionHeight);
						gridY += descriptionHeight + scale(4);
					}
					const auto gridHeight = scale(row->Height ? row->Height : 200) + gridExtra;
					place(row->Control, pad, gridY, inner, gridHeight);
					rowHeight = gridY + gridHeight - y;
					break;
				}

				case Row::RowType::Check: {
					// Being renamed (BeginSessionRename): the box on a line of its own, so as not to cover the buttons beside it.
					if (row->Secondary) {
						place(row->Secondary, pad, y, std::min(inner, scale(320)), controlHeight);
						place(row->Status, 0, 0, 0, 0);
						rowHeight = controlHeight;
						break;
					}
					// The button holds its label, wrapped at the page's width, with a pixel above and below for the focus; a group's heading is spaced as one.
					if (row->Bold) {
						y += scale(8);
						SelectObject(hdc, *m_boldFont);
					}
					const auto text = GetWindowString(row->Control);
					// Where the button draws its label, to line the description up: glyph plus a third of it, as the theme and CustomDrawDarkButton do.
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

					const auto rowHelpSpace = row->Help ? helpSpace : 0;
					if (row->FlowGroup) {
						if (previousFlowGroup == row->FlowGroup && flowX + singleWidth + rowHelpSpace <= pad + inner)
							y -= lineHeight + 2 + rowGap;
						else
							flowX = pad;
						const auto flowWidth = std::min(singleWidth, std::max(0, pad + inner - flowX - rowHelpSpace));
						place(row->Control, flowX, y, flowWidth, lineHeight + 2);
						place(row->Help, flowX + flowWidth + scale(4), y + 1, helpWidth, lineHeight);
						place(row->Status, 0, 0, 0, 0);
						flowX += flowWidth + rowHelpSpace + scale(14);
						flowGroup = row->FlowGroup;
						rowHeight = lineHeight + 2;
						break;
					}

					RECT rcText{0, 0, std::max(1, inner - textX - scale(6) - rowHelpSpace), 0};
					DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rcText, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);
					const auto textHeight = std::max(lineHeight, static_cast<int>(rcText.bottom)) + 2;
					const auto labelX = pad + textX;
					const auto labelWidth = inner - textX;
					const auto descriptionHeight = measure(row->Description, labelWidth);

					// A menu button at the line's right end, the check box beside it in the line's middle.
					auto controlWidth = std::min(inner - rowHelpSpace, singleWidth);
					auto controlY = y;
					auto lineHeightUsed = textHeight;
					auto lineEnd = pad + inner;
					if (row->Button1) {
						RECT rcButton{};
						const auto buttonText = GetWindowString(row->Button1);
						DrawTextW(hdc, buttonText.c_str(), static_cast<int>(buttonText.size()), &rcButton, DT_CALCRECT | DT_SINGLELINE);
						const auto menuWidth = std::max(scale(72), static_cast<int>(rcButton.right) + scale(24));
						place(row->Button1, pad + inner - menuWidth, y, menuWidth, controlHeight);
						controlWidth = std::min(controlWidth, std::max(0, inner - menuWidth - gap - rowHelpSpace));
						controlY = y + std::max(0, (controlHeight - textHeight) / 2);
						lineHeightUsed = std::max(textHeight, controlHeight);
						lineEnd -= menuWidth + gap;
					}
					place(row->Control, pad, controlY, controlWidth, textHeight);
					if (textHeight <= lineHeight + 2 && !row->Button1 && !row->Description)
						besideX = pad + controlWidth + rowHelpSpace + gap;
					place(row->Help, pad + controlWidth + scale(4), controlY + 1, helpWidth, lineHeight);

					// At the end of the check box's line if that is a single one with room for it, or else on lines of its own under it.
					auto belowY = y + lineHeightUsed + scale(2);
					if (const auto statusWidth = textWidth(row->Status); !statusWidth) {
						place(row->Status, 0, 0, 0, 0);
					} else if (textHeight <= lineHeight + 2 && pad + controlWidth + rowHelpSpace + gap + statusWidth <= lineEnd) {
						place(row->Status, lineEnd - statusWidth, controlY + 1, statusWidth, lineHeight);
					} else {
						const auto statusHeight = measure(row->Status, labelWidth);
						place(row->Status, labelX, belowY, labelWidth, statusHeight);
						belowY += statusHeight + scale(2);
					}
					place(row->Description, labelX, belowY, labelWidth, descriptionHeight);
					rowHeight = descriptionHeight ? belowY + descriptionHeight - y : belowY - scale(2) - y;
					break;
				}

				case Row::RowType::PathList: {
					const auto labelHeight = placeLabel(*row, pad, y, inner);
					const auto descriptionHeight = measure(row->Description, inner);
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
					const auto labelWidth = inner * 45 / 100;
					const auto controlX = pad + labelWidth + gap;
					const auto controlWidth = inner - labelWidth - gap;
					const auto labelY = y + std::max(0, (controlHeight - lineHeight) / 2);
					const auto labelHeight = placeLabel(*row, pad, labelY, labelWidth);
					const auto descriptionHeight = measure(row->Description, inner);
					const auto descriptionY = std::max(labelY + labelHeight, y + controlHeight) + scale(2);
					place(row->Description, pad, descriptionY, inner, descriptionHeight);
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
					rowHeight = std::max(controlHeight, labelY - y + labelHeight);
					if (descriptionHeight)
						rowHeight = descriptionY - y + descriptionHeight;
					break;
				}
			}
			lastRowY = y;
			lastRowHeight = rowHeight;
			y += rowHeight + rowGap;
		}
		return y;
	};
	const auto isShown = [](const auto& row) { return !row->TabOwner || row->TabIndex == row->TabOwner->ActiveTab; };
	auto y = layoutRows(0);
	if (const auto spare = static_cast<int>(rcPage.bottom) - (y - rowGap + pad); spare > 0) {
		if (const auto grids = static_cast<int>(std::ranges::count_if(m_rows, [&isShown](const auto& row) { return row->Type == Row::RowType::Grid && isShown(row); })))
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
}

void XivAlexander::Apps::MainApp::Window::SettingsView::ScrollPageTo(int y) {
	RECT rcPage;
	GetClientRect(m_hPage, &rcPage);
	y = std::clamp(y, 0, std::max(0, m_contentHeight - static_cast<int>(rcPage.bottom)));
	if (y == m_scrollY)
		return;
	m_scrollY = y;
	ShowHelp(nullptr, false);
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
			const auto& dis = *reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
			const auto isHelp = std::ranges::any_of(m_rows, [&dis](const auto& row) { return row->Help == dis.hwndItem; });
			const auto isLink = isHelp || std::ranges::any_of(m_rows, [&dis](const auto& row) { return row->LinkButton && row->Button1 == dis.hwndItem; });
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
			const auto text = isHelp ? std::wstring(HelpMark) : GetWindowString(dis.hwndItem);
			RECT rc = dis.rcItem;
			DrawTextW(dis.hDC, text.c_str(), static_cast<int>(text.size()), &rc, DT_SINGLELINE | (isHelp ? DT_TOP : DT_VCENTER) | DT_CENTER | DT_NOPREFIX);
			if ((dis.itemState & ODS_FOCUS) && !(dis.itemState & ODS_NOFOCUSRECT))
				DrawFocusRect(dis.hDC, &dis.rcItem);
			DeleteObject(SelectObject(dis.hDC, hPrevFont));
			return TRUE;
		}

		case WM_SETCURSOR:
			if (std::ranges::any_of(m_rows, [wParam](const auto& row) { return (row->LinkButton && row->Button1 == reinterpret_cast<HWND>(wParam)) || row->Help == reinterpret_cast<HWND>(wParam); })) {
				SetCursor(LoadCursorW(nullptr, IDC_HAND));
				return TRUE;
			}
			break;

		case WM_CONTEXTMENU: {
			// A disabled control's right click comes from the page; ChildWindowFromPointEx finds disabled ones too.
			auto target = reinterpret_cast<HWND>(wParam);
			if (target == hwnd && (GET_X_LPARAM(lParam) != -1 || GET_Y_LPARAM(lParam) != -1)) {
				POINT ptClient{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				ScreenToClient(hwnd, &ptClient);
				if (const auto hChild = ChildWindowFromPointEx(hwnd, ptClient, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT))
					target = hChild;
			}
			for (const auto& row : m_rows) {
				if (row->ContextMenu && (row->Control == target || row->Label == target)) {
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
		}

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
			// The background takes the focus so a click on it ends any edit; dragging it moves the window as its title bar would.
			SetFocus(hwnd);
			DragWindow();
			return 0;

		case WM_HSCROLL:
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
	if (part == PartHelp) {
		// Focused by a click, which then toggles it; focused from the keyboard, shown at once.
		if (notification == BN_CLICKED) {
			ShowHelp(row.Help, m_trackedHelp != row.Help);
		} else if (notification == BN_SETFOCUS) {
			ScrollIntoView(row.Help);
			if (GetKeyState(VK_LBUTTON) >= 0)
				ShowHelp(row.Help, true);
		} else if (notification == BN_KILLFOCUS && m_trackedHelp == row.Help) {
			ShowHelp(nullptr, false);
		}
		return;
	}

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
			const auto weak = std::ranges::any_of(m_rows, [hControl](const auto& row) { return row->Description == hControl || row->Status == hControl; });
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
				// Only folders and ModPacks, which are directories, can be renamed; returning true refuses.
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
			if (const auto handler = m_app.GetNetworkTimingHandler()) {
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

		case WmListenToCrowdFix:
			ListenToCrowdFix();
			RefreshRowStatuses();
			return 0;

		case WmCrowdFixStatusChanged:
			RefreshRowStatuses();
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

		case WM_DESTROY:
			// Nothing is shown any more; the object goes with its host.
			m_cooldownListener.clear();
			m_crowdFixListener.clear();
			m_rowCleanup.clear();
			m_cleanup.clear();
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
	if (m_hTooltip) {
		SendMessageW(m_hTooltip, WM_SETFONT, reinterpret_cast<WPARAM>(*font), TRUE);
		SendMessageW(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(400 * zoom));
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
	if (m_hTooltip)
		ApplyThemeToControl(m_hTooltip);
	EnumChildWindows(m_hPage, [](HWND hChild, LPARAM lParam) {
		reinterpret_cast<const SettingsView*>(lParam)->ApplyThemeToControl(hChild);
		return TRUE;
	}, reinterpret_cast<LPARAM>(this));
	for (const auto& row : m_rows) {
		if (row->Grid)
			row->Grid->ApplyTheme(dark);
	}
	RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}
