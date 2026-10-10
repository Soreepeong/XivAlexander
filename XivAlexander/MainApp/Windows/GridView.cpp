#include "pch.h"
#include "MainApp/Windows/GridView.h"
#include "MainApp/Windows/ThemeColors.h"

#include "XivAlexander.h"

namespace {
	constexpr auto ContainerClassName = L"XivAlexander::Window::GridView";
	constexpr UINT_PTR ListSubclassId = 1;
	constexpr UINT_PTR EditorSubclassId = 2;
	constexpr UINT WmSelectionChanged = WM_APP + 1;  // To the container.

	std::wstring GetText(HWND hWnd) {
		std::wstring text(static_cast<size_t>(GetWindowTextLengthW(hWnd)) + 1, L'\0');
		text.resize(GetWindowTextW(hWnd, text.data(), static_cast<int>(text.size())));
		return text;
	}
}

XivAlexander::Apps::MainApp::Window::GridView::GridView(HWND hParent, UINT id, std::vector<Column> columns, Source source)
	: m_columns(std::move(columns))
	, m_source(std::move(source)) {
	const WNDCLASSEXW wcex{
		.cbSize = sizeof(WNDCLASSEXW),
		.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
			if (const auto self = reinterpret_cast<GridView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
				return self->ContainerProc(uMsg, wParam, lParam);
			return DefWindowProcW(hwnd, uMsg, wParam, lParam);
		},
		.hInstance = Dll::Module(),
		.hCursor = LoadCursorW(nullptr, IDC_ARROW),
		.lpszClassName = ContainerClassName,
	};
	if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		throw Utils::Win32::Error("RegisterClassExW");

	m_hWnd = CreateWindowExW(WS_EX_CONTROLPARENT, ContainerClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 0, 0, 0, hParent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), Dll::Module(), nullptr);
	if (!m_hWnd)
		throw Utils::Win32::Error("CreateWindowExW");
	SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	m_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | (m_source.MultiSelect ? 0 : LVS_SINGLESEL),
		0, 0, 0, 0, m_hWnd, nullptr, Dll::Module(), nullptr);
	ListView_SetExtendedListViewStyle(m_hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	for (size_t i = 0; i < m_columns.size(); ++i) {
		LVCOLUMNW col{
			.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT,
			.fmt = m_columns[i].Kind == CellKind::Check ? LVCFMT_CENTER : LVCFMT_LEFT,
			.cx = m_columns[i].Width,
			.pszText = const_cast<wchar_t*>(m_columns[i].Title.c_str()),
		};
		ListView_InsertColumn(m_hList, static_cast<int>(i), &col);
	}
	SetWindowSubclass(m_hList, ListSubclassProc, ListSubclassId, reinterpret_cast<DWORD_PTR>(this));

	Refresh();
}

XivAlexander::Apps::MainApp::Window::GridView::~GridView() {
	m_editor.reset();
	if (IsWindow(m_hWnd))
		DestroyWindow(m_hWnd);
}

void XivAlexander::Apps::MainApp::Window::GridView::Refresh() {
	const auto count = GetRowCount();
	ListView_SetItemCountEx(m_hList, static_cast<int>(count), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	if (count && m_row >= count)
		m_row = count - 1;
	InvalidateRect(m_hList, nullptr, FALSE);
}

std::optional<size_t> XivAlexander::Apps::MainApp::Window::GridView::GetSelectedRow() const {
	if (const auto index = ListView_GetNextItem(m_hList, -1, LVNI_SELECTED); index >= 0 && static_cast<size_t>(index) < GetRowCount())
		return static_cast<size_t>(index);
	return std::nullopt;
}

std::vector<size_t> XivAlexander::Apps::MainApp::Window::GridView::GetSelectedRows() const {
	std::vector<size_t> rows;
	const auto count = GetRowCount();
	for (auto index = ListView_GetNextItem(m_hList, -1, LVNI_SELECTED); index >= 0; index = ListView_GetNextItem(m_hList, index, LVNI_SELECTED)) {
		if (static_cast<size_t>(index) < count)
			rows.push_back(static_cast<size_t>(index));
	}
	return rows;
}

void XivAlexander::Apps::MainApp::Window::GridView::Select(size_t row) {
	if (row >= GetRowCount())
		return;
	MoveCurrent(row, m_column);
}

void XivAlexander::Apps::MainApp::Window::GridView::ClearSelection() {
	ListView_SetItemState(m_hList, -1, 0, LVIS_SELECTED);
}

void XivAlexander::Apps::MainApp::Window::GridView::MoveCurrent(size_t row, size_t column) {
	const auto count = GetRowCount();
	if (!count || m_columns.empty())
		return;
	row = std::min(row, count - 1);
	column = std::min(column, m_columns.size() - 1);
	const auto previousRow = m_row;
	m_row = row;
	m_column = column;
	ListView_SetItemState(m_hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_SetItemState(m_hList, static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_EnsureVisible(m_hList, static_cast<int>(row), FALSE);
	ListView_RedrawItems(m_hList, static_cast<int>(std::min(previousRow, row)), static_cast<int>(std::max(previousRow, row)));
}

bool XivAlexander::Apps::MainApp::Window::GridView::IsEditable(size_t row, size_t column) const {
	if (column >= m_columns.size() || row >= GetRowCount() || m_columns[column].Kind == CellKind::ReadOnly)
		return false;
	return !m_source.IsEditable || m_source.IsEditable(row, column);
}

RECT XivAlexander::Apps::MainApp::Window::GridView::GetCellRect(size_t row, size_t column) const {
	RECT rc{};
	ListView_GetSubItemRect(m_hList, static_cast<int>(row), static_cast<int>(column), column == 0 ? LVIR_LABEL : LVIR_BOUNDS, &rc);
	if (column == 0) {
		// The label of the first column leaves out the space for an icon; the cell starts where the row does.
		RECT rcRow{};
		ListView_GetItemRect(m_hList, static_cast<int>(row), &rcRow, LVIR_BOUNDS);
		rc.left = rcRow.left;
	}
	return rc;
}

std::optional<std::pair<size_t, size_t>> XivAlexander::Apps::MainApp::Window::GridView::HitTest(POINT ptClient) const {
	LVHITTESTINFO hti{.pt = ptClient};
	if (ListView_SubItemHitTest(m_hList, &hti) < 0 || hti.iItem < 0 || hti.iSubItem < 0)
		return std::nullopt;
	return std::pair{static_cast<size_t>(hti.iItem), static_cast<size_t>(hti.iSubItem)};
}

void XivAlexander::Apps::MainApp::Window::GridView::Toggle(size_t row, size_t column) {
	if (!IsEditable(row, column) || m_columns[column].Kind != CellKind::Check || !m_source.GetChecked || (!m_source.SetChecked && !m_source.SetCheckedRows))
		return;

	// The selected rows together, if this is one of them: all as this one becomes.
	auto rows = m_source.MultiSelect ? GetSelectedRows() : std::vector<size_t>();
	if (std::ranges::find(rows, row) == rows.end())
		rows = {row};
	std::erase_if(rows, [this, column](size_t r) { return !IsEditable(r, column); });
	const auto checked = !m_source.GetChecked(row, column);
	if (m_source.SetCheckedRows) {
		m_source.SetCheckedRows(rows, column, checked);
	} else {
		for (const auto r : rows)
			m_source.SetChecked(r, column, checked);
	}
	InvalidateRect(m_hList, nullptr, FALSE);
}

void XivAlexander::Apps::MainApp::Window::GridView::MoveRow(size_t from, size_t to) {
	if (!m_source.MoveRow || from >= GetRowCount() || to > GetRowCount() || to == from || to == from + 1)
		return;
	m_source.MoveRow(from, to);
	Refresh();
	MoveCurrent(to > from ? to - 1 : to, m_column);
}

void XivAlexander::Apps::MainApp::Window::GridView::CommitEdit() {
	EndEdit(EndEditMode::ApplyOrCancel);
}

size_t XivAlexander::Apps::MainApp::Window::GridView::GetDropTarget(POINT ptClient) const {
	// Before the row under the point if in its upper half, else after it; last if below the rows.
	const auto count = GetRowCount();
	if (!count)
		return 0;
	LVHITTESTINFO hti{.pt = ptClient};
	if (const auto index = ListView_HitTest(m_hList, &hti); index >= 0) {
		RECT rc{};
		ListView_GetItemRect(m_hList, index, &rc, LVIR_BOUNDS);
		return static_cast<size_t>(index) + (ptClient.y >= (rc.top + rc.bottom) / 2 ? 1 : 0);
	}
	RECT rcLast{};
	ListView_GetItemRect(m_hList, static_cast<int>(count - 1), &rcLast, LVIR_BOUNDS);
	return ptClient.y >= rcLast.bottom ? count : static_cast<size_t>(std::max(0, ListView_GetTopIndex(m_hList)));
}

void XivAlexander::Apps::MainApp::Window::GridView::EndDrag(bool drop) {
	const auto from = m_dragRow;
	const auto to = m_dropBefore;
	m_dragRow.reset();
	m_dropBefore.reset();
	if (GetCapture() == m_hList)
		ReleaseCapture();
	InvalidateRect(m_hList, nullptr, FALSE);
	if (drop && from && to)
		MoveRow(*from, *to);
}

void XivAlexander::Apps::MainApp::Window::GridView::BeginEdit(size_t row, size_t column, std::optional<wchar_t> typed) {
	if (m_editor)
		EndEdit(EndEditMode::ApplyOrCancel);
	if (!IsEditable(row, column))
		return;

	const auto kind = m_columns[column].Kind;
	if (kind == CellKind::Check) {
		Toggle(row, column);
		return;
	}

	MoveCurrent(row, column);
	const auto rc = GetCellRect(row, column);
	const auto text = typed ? std::wstring(1, *typed) : m_source.GetEditText ? m_source.GetEditText(row, column) : m_source.GetText(row, column);

	Editor editor{.Row = row, .Column = column};
	if (kind == CellKind::Text) {
		editor.Window = editor.Input = CreateWindowExW(0, WC_EDITW, text.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
			rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, m_hList, nullptr, Dll::Module(), nullptr);
	} else {
		const auto style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | (kind == CellKind::ComboChoice ? CBS_DROPDOWNLIST : CBS_DROPDOWN | CBS_AUTOHSCROLL);
		editor.Window = CreateWindowExW(0, WC_COMBOBOXW, L"", style,
			rc.left, rc.top, rc.right - rc.left, static_cast<int>(240 * m_zoom), m_hList, nullptr, Dll::Module(), nullptr);
		const auto choices = m_columns[column].GetChoices ? m_columns[column].GetChoices(row) : std::vector<std::wstring>();
		for (const auto& choice : choices)
			ComboBox_AddString(editor.Window, choice.c_str());
		if (kind == CellKind::ComboChoice) {
			ComboBox_SelectString(editor.Window, -1, text.c_str());
			editor.Input = editor.Window;
		} else {
			SetWindowTextW(editor.Window, text.c_str());
			COMBOBOXINFO cbi{.cbSize = sizeof(COMBOBOXINFO)};
			GetComboBoxInfo(editor.Window, &cbi);
			editor.Input = cbi.hwndItem ? cbi.hwndItem : editor.Window;
		}
	}
	if (!editor.Window)
		return;

	SendMessageW(editor.Window, WM_SETFONT, reinterpret_cast<WPARAM>(m_hFont), FALSE);
	ApplyDarkModeToControl(editor.Window, m_dark);
	SetWindowSubclass(editor.Input, EditorSubclassProc, EditorSubclassId, reinterpret_cast<DWORD_PTR>(this));
	m_editor = editor;

	SetFocus(editor.Input);
	if (kind == CellKind::Text || kind == CellKind::ComboEdit) {
		// Typing replaces the text, and goes on after what was typed; F2 and the others select it all.
		const auto length = static_cast<int>(text.size());
		SendMessageW(editor.Input, EM_SETSEL, typed ? length : 0, length);
	} else {
		ComboBox_ShowDropdown(editor.Window, TRUE);
	}
}

bool XivAlexander::Apps::MainApp::Window::GridView::EndEdit(EndEditMode mode) {
	if (!m_editor || m_endingEdit)
		return true;

	const auto editor = *m_editor;
	if (mode != EndEditMode::Cancel && m_source.SetText) {
		std::wstring text;
		if (m_columns[editor.Column].Kind == CellKind::ComboChoice) {
			if (const auto selected = ComboBox_GetCurSel(editor.Window); selected >= 0) {
				text.resize(static_cast<size_t>(ComboBox_GetLBTextLen(editor.Window, selected)) + 1);
				text.resize(ComboBox_GetLBText(editor.Window, selected, text.data()));
			} else {
				mode = EndEditMode::Cancel;
			}
		} else {
			text = GetText(editor.Window);
		}
		if (mode != EndEditMode::Cancel && !m_source.SetText(editor.Row, editor.Column, text)) {
			MessageBeep(MB_ICONWARNING);
			if (mode == EndEditMode::Apply)
				return false;
		}
	}

	// Destroying the editor takes its focus, and would end it again.
	m_endingEdit = true;
	const auto hadFocus = GetFocus() == editor.Input || GetFocus() == editor.Window;
	m_editor.reset();
	DestroyWindow(editor.Window);
	m_endingEdit = false;
	if (hadFocus)
		SetFocus(m_hList);
	ListView_RedrawItems(m_hList, static_cast<int>(editor.Row), static_cast<int>(editor.Row));
	return true;
}

LRESULT CALLBACK XivAlexander::Apps::MainApp::Window::GridView::ListSubclassProc(HWND, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
	return reinterpret_cast<GridView*>(ref)->ListProc(uMsg, wParam, lParam);
}

LRESULT CALLBACK XivAlexander::Apps::MainApp::Window::GridView::EditorSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
	return reinterpret_cast<GridView*>(ref)->EditorProc(hWnd, uMsg, wParam, lParam);
}

LRESULT XivAlexander::Apps::MainApp::Window::GridView::EditorProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_GETDLGCODE:
			// The keys are the editor's, and not the dialog's: Enter, Esc, and Tab end the edit.
			return DefSubclassProc(hWnd, uMsg, wParam, lParam) | DLGC_WANTALLKEYS;

		case WM_KEYDOWN:
			if (!m_editor)
				break;
			switch (wParam) {
				case VK_RETURN:
					// An open drop-down list takes the choice first.
					if (m_columns[m_editor->Column].Kind != CellKind::Text && ComboBox_GetDroppedState(m_editor->Window))
						ComboBox_ShowDropdown(m_editor->Window, FALSE);
					EndEdit(EndEditMode::Apply);
					return 0;

				case VK_ESCAPE:
					EndEdit(EndEditMode::Cancel);
					return 0;

				case VK_TAB: {
					const auto row = m_editor->Row;
					const auto column = m_editor->Column;
					if (EndEdit(EndEditMode::Apply)) {
						const auto back = GetKeyState(VK_SHIFT) < 0;
						if (back ? column > 0 : column + 1 < m_columns.size())
							MoveCurrent(row, back ? column - 1 : column + 1);
					}
					return 0;
				}
			}
			break;

		case WM_CHAR:
			if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB)
				return 0;
			break;

		case WM_KILLFOCUS: {
			// Another window of the editor's (the list of a combo box, or the box itself) keeps the edit going.
			const auto hNew = reinterpret_cast<HWND>(wParam);
			if (m_editor && hNew != m_editor->Window && hNew != m_editor->Input && GetParent(hNew) != m_editor->Window) {
				const auto result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
				EndEdit(EndEditMode::ApplyOrCancel);
				return result;
			}
			break;
		}

		case WM_NCDESTROY:
			RemoveWindowSubclass(hWnd, EditorSubclassProc, EditorSubclassId);
			break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT XivAlexander::Apps::MainApp::Window::GridView::ListProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_GETDLGCODE: {
			// The arrows and characters move and edit the current cell; Enter edits it, rather than being the dialog's.
			auto code = DefSubclassProc(m_hList, uMsg, wParam, lParam) | DLGC_WANTARROWS | DLGC_WANTCHARS;
			if (const auto msg = reinterpret_cast<const MSG*>(lParam); msg && msg->message == WM_KEYDOWN && (msg->wParam == VK_RETURN || (msg->wParam == VK_ESCAPE && m_dragRow)))
				code |= DLGC_WANTMESSAGE;
			return code;
		}

		case WM_KEYDOWN: {
			if (m_source.KeyDown && m_source.KeyDown(static_cast<UINT>(wParam)))
				return 0;
			const auto ctrl = GetKeyState(VK_CONTROL) < 0;
			if (m_source.MultiSelect && ctrl && wParam == 'A') {
				ListView_SetItemState(m_hList, -1, LVIS_SELECTED, LVIS_SELECTED);
				return 0;
			}
			switch (wParam) {
				case VK_LEFT:
					if (m_column > 0)
						MoveCurrent(m_row, m_column - 1);
					return 0;
				case VK_RIGHT:
					if (m_column + 1 < m_columns.size())
						MoveCurrent(m_row, m_column + 1);
					return 0;
				case VK_UP:
				case VK_DOWN:
					if (ctrl && m_source.MoveRow) {
						if (wParam == VK_UP && m_row > 0)
							MoveRow(m_row, m_row - 1);
						else if (wParam == VK_DOWN && m_row + 1 < GetRowCount())
							MoveRow(m_row, m_row + 2);
						return 0;
					}
					break;
				case VK_F2:
					BeginEdit(m_row, m_column);
					return 0;
				case VK_RETURN:
					if (!IsEditable(m_row, m_column) && m_source.Activate && m_row < GetRowCount())
						m_source.Activate(m_row);
					else
						BeginEdit(m_row, m_column);
					return 0;
				case VK_SPACE:
					if (m_column < m_columns.size() && m_columns[m_column].Kind == CellKind::Check) {
						Toggle(m_row, m_column);
						return 0;
					}
					break;
				case VK_ESCAPE:
					if (m_dragRow) {
						EndDrag(false);
						return 0;
					}
					break;
			}
			break;
		}

		case WM_CHAR:
			// Typing edits the current cell with what was typed, as a spreadsheet does.
			if (wParam >= L' ' && m_column < m_columns.size() && (m_columns[m_column].Kind == CellKind::Text || m_columns[m_column].Kind == CellKind::ComboEdit)
				&& GetKeyState(VK_CONTROL) >= 0 && IsEditable(m_row, m_column)) {
				BeginEdit(m_row, m_column, static_cast<wchar_t>(wParam));
				return 0;
			}
			if (wParam == L' ')
				return 0;
			break;

		case WM_MOUSEWHEEL: {
			EndEdit(EndEditMode::ApplyOrCancel);

			// Scrolls the list while it can that way, and then what the list is in, as nested scrolling goes elsewhere: the
			// list would otherwise take every turn of the wheel over it, even with nothing left to scroll.
			SCROLLINFO si{.cbSize = sizeof si, .fMask = SIF_RANGE | SIF_PAGE | SIF_POS};
			const auto scrollable = (GetWindowLongPtrW(m_hList, GWL_STYLE) & WS_VSCROLL) && GetScrollInfo(m_hList, SB_VERT, &si);
			const auto up = GET_WHEEL_DELTA_WPARAM(wParam) > 0;
			const auto atEnd = !scrollable || (up ? si.nPos <= si.nMin : si.nPos + static_cast<int>(si.nPage) > si.nMax);
			if (atEnd) {
				if (const auto hOuter = GetParent(m_hWnd))
					return SendMessageW(hOuter, WM_MOUSEWHEEL, wParam, lParam);
			}
			break;
		}

		case WM_VSCROLL:
		case WM_HSCROLL:
			EndEdit(EndEditMode::ApplyOrCancel);
			break;

		case WM_LBUTTONDOWN: {
			EndEdit(EndEditMode::ApplyOrCancel);
			const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			if (const auto hit = HitTest(pt)) {
				const auto [row, column] = *hit;
				const auto isCheck = column < m_columns.size() && m_columns[column].Kind == CellKind::Check;
				if (m_source.MultiSelect && (wParam & (MK_CONTROL | MK_SHIFT))) {
					const auto result = DefSubclassProc(m_hList, uMsg, wParam, lParam);
					m_row = row;
					m_column = column;
					InvalidateRect(m_hList, nullptr, FALSE);
					return result;
				}
				if (m_source.MultiSelect && isCheck && ListView_GetItemState(m_hList, static_cast<int>(row), LVIS_SELECTED) && ListView_GetSelectedCount(m_hList) > 1) {
					SetFocus(m_hList);
					m_row = row;
					m_column = column;
					Toggle(row, column);
					return 0;
				}
				const auto wasCurrent = row == m_row && column == m_column && GetFocus() == m_hList;
				const auto result = DefSubclassProc(m_hList, uMsg, wParam, lParam);  // Selects, focuses, and may begin a drag.
				if (m_dragRow)
					return result;
				MoveCurrent(row, column);
				if (column < m_columns.size() && m_columns[column].Kind == CellKind::Check)
					Toggle(row, column);
				else if (wasCurrent)
					BeginEdit(row, column);
				return result;
			}
			break;
		}

		case WM_LBUTTONDBLCLK: {
			const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			if (const auto hit = HitTest(pt)) {
				if (hit->second < m_columns.size() && m_columns[hit->second].Kind == CellKind::Check)
					Toggle(hit->first, hit->second);
				else if (m_source.Activate)
					m_source.Activate(hit->first);
				else
					BeginEdit(hit->first, hit->second);
				return 0;
			}
			break;
		}

		case WM_CONTEXTMENU:
			if (m_source.ShowContextMenu) {
				EndEdit(EndEditMode::ApplyOrCancel);
				POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				std::optional<size_t> row;
				if (pt.x == -1 && pt.y == -1) {
					// From the keyboard: at the current cell.
					if (m_row < GetRowCount()) {
						row = m_row;
						const auto rc = GetCellRect(m_row, m_column);
						pt = {rc.left, rc.bottom};
						ClientToScreen(m_hList, &pt);
					}
				} else {
					auto ptClient = pt;
					ScreenToClient(m_hList, &ptClient);
					if (const auto hit = HitTest(ptClient)) {
						row = hit->first;
						// A selected row keeps the others selected with it, for the menu to act on them all.
						if (m_source.MultiSelect && ListView_GetItemState(m_hList, static_cast<int>(hit->first), LVIS_SELECTED)) {
							m_row = hit->first;
							m_column = hit->second;
						} else {
							MoveCurrent(hit->first, hit->second);
						}
					}
				}
				if (row)
					m_source.ShowContextMenu(*row, pt);
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
			if (m_dragRow) {
				const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
				if (const auto target = GetDropTarget(pt); target != m_dropBefore) {
					m_dropBefore = target;
					InvalidateRect(m_hList, nullptr, FALSE);
				}

				// Near the edges, the list scrolls.
				RECT rc;
				GetClientRect(m_hList, &rc);
				RECT rcHeader{};
				GetWindowRect(ListView_GetHeader(m_hList), &rcHeader);
				const auto top = static_cast<int>(rcHeader.bottom - rcHeader.top);
				const auto margin = static_cast<int>(12 * m_zoom);
				if (pt.y < top + margin)
					ListView_Scroll(m_hList, 0, -margin);
				else if (pt.y > rc.bottom - margin)
					ListView_Scroll(m_hList, 0, margin);
				return 0;
			}
			break;

		case WM_LBUTTONUP:
			if (m_dragRow) {
				EndDrag(true);
				return 0;
			}
			break;

		case WM_CAPTURECHANGED:
			if (m_dragRow && reinterpret_cast<HWND>(lParam) != m_hList)
				EndDrag(false);
			break;

		case WM_NOTIFY: {
			// The header's, in dark mode: its text in the theme's color.
			const auto nmhdr = reinterpret_cast<LPNMHDR>(lParam);
			if (m_dark && nmhdr->hwndFrom == ListView_GetHeader(m_hList) && nmhdr->code == NM_CUSTOMDRAW) {
				const auto nmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
				if (nmcd->dwDrawStage == CDDS_PREPAINT)
					return CDRF_NOTIFYITEMDRAW;
				if (nmcd->dwDrawStage == CDDS_ITEMPREPAINT) {
					SetTextColor(nmcd->hdc, GetThemeColors(true).GetForeground());
					return CDRF_DODEFAULT;
				}
			}
			break;
		}

		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
		case WM_CTLCOLORSTATIC:
			if (m_dark) {
				const auto& colors = GetThemeColors(true);
				SetTextColor(reinterpret_cast<HDC>(wParam), colors.GetForeground());
				SetBkColor(reinterpret_cast<HDC>(wParam), colors.GetBackground());
				return reinterpret_cast<LRESULT>(*m_backgroundBrush);
			}
			break;

		case WM_SETFOCUS:
		case WM_KILLFOCUS:
			// The current cell is outlined while the list has the focus.
			if (const auto count = GetRowCount(); m_row < count)
				ListView_RedrawItems(m_hList, static_cast<int>(m_row), static_cast<int>(m_row));
			break;

		case WM_NCDESTROY:
			RemoveWindowSubclass(m_hList, ListSubclassProc, ListSubclassId);
			break;
	}
	return DefSubclassProc(m_hList, uMsg, wParam, lParam);
}

LRESULT XivAlexander::Apps::MainApp::Window::GridView::ContainerProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WmSelectionChanged:
			m_selectionChangePosted = false;
			if (m_source.SelectionChanged)
				m_source.SelectionChanged(GetSelectedRow());
			return 0;

		case WM_SIZE:
			SetWindowPos(m_hList, nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
			UpdateColumnWidths();
			return 0;

		case WM_SETFOCUS:
			SetFocus(m_hList);
			return 0;

		case WM_NOTIFY: {
			const auto nmhdr = reinterpret_cast<LPNMHDR>(lParam);
			if (nmhdr->hwndFrom != m_hList)
				break;
			switch (nmhdr->code) {
				case LVN_GETDISPINFOW: {
					auto& item = reinterpret_cast<NMLVDISPINFOW*>(lParam)->item;
					if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0) {
						std::wstring text;
						if (const auto row = static_cast<size_t>(item.iItem), column = static_cast<size_t>(item.iSubItem);
							row < GetRowCount() && column < m_columns.size() && m_columns[column].Kind != CellKind::Check && m_source.GetText)
							text = m_source.GetText(row, column);
						wcsncpy_s(item.pszText, item.cchTextMax, text.c_str(), _TRUNCATE);
					}
					return 0;
				}

				case LVN_BEGINDRAG:
					if (m_source.MoveRow && !m_editor && GetRowCount() > 1) {
						m_dragRow = static_cast<size_t>(reinterpret_cast<LPNMLISTVIEW>(lParam)->iItem);
						m_dropBefore.reset();
						SetCapture(m_hList);
						SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
					}
					return 0;

				case LVN_ITEMCHANGED: {
					const auto nmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
					if (nmlv->iItem >= 0 && (nmlv->uNewState & LVIS_FOCUSED) && !(nmlv->uOldState & LVIS_FOCUSED))
						m_row = static_cast<size_t>(nmlv->iItem);
					if (m_source.SelectionChanged && ((nmlv->uNewState ^ nmlv->uOldState) & LVIS_SELECTED) && !m_selectionChangePosted) {
						// Once, for the unselecting and selecting of one change.
						m_selectionChangePosted = true;
						PostMessageW(m_hWnd, WmSelectionChanged, 0, 0);
					}
					return 0;
				}

				case NM_CUSTOMDRAW:
					if (const auto result = OnCustomDraw(*reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam)))
						return *result;
					break;
			}
			break;
		}
	}
	return DefWindowProcW(m_hWnd, uMsg, wParam, lParam);
}

std::optional<LRESULT> XivAlexander::Apps::MainApp::Window::GridView::OnCustomDraw(NMLVCUSTOMDRAW& nmcd) {
	switch (nmcd.nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;

		case CDDS_POSTPAINT:
			// Where a dragged row would go: a line between rows.
			if (m_dragRow && m_dropBefore) {
				if (const auto count = GetRowCount()) {
					RECT rc{};
					const auto index = std::min(*m_dropBefore, count - 1);
					ListView_GetItemRect(m_hList, static_cast<int>(index), &rc, LVIR_BOUNDS);
					const auto y = *m_dropBefore < count ? rc.top : rc.bottom;
					const auto thickness = std::max(2, static_cast<int>(2 * m_zoom));
					RECT rcClient;
					GetClientRect(m_hList, &rcClient);
					RECT rcLine{rcClient.left, y - thickness / 2, rcClient.right, y - thickness / 2 + thickness};
					const auto hBrush = CreateSolidBrush(GetThemeColors(m_dark).GetForeground());
					FillRect(nmcd.nmcd.hdc, &rcLine, hBrush);
					DeleteObject(hBrush);
				}
			}
			return CDRF_DODEFAULT;

		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;

		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
			if (m_dark) {
				nmcd.clrText = GetThemeColors(true).GetForeground();
				return CDRF_NEWFONT | CDRF_NOTIFYPOSTPAINT;
			}
			return CDRF_NOTIFYPOSTPAINT;

		case CDDS_ITEMPOSTPAINT | CDDS_SUBITEM: {
			const auto row = static_cast<size_t>(nmcd.nmcd.dwItemSpec);
			const auto column = static_cast<size_t>(nmcd.iSubItem);
			if (row >= GetRowCount() || column >= m_columns.size())
				return CDRF_DODEFAULT;
			const auto rc = GetCellRect(row, column);

			// A checkbox, centered, over the empty text.
			if (m_columns[column].Kind == CellKind::Check && m_source.GetChecked) {
				if (const auto hTheme = OpenThemeData(m_hList, L"Button")) {
					const auto checked = m_source.GetChecked(row, column);
					const auto state = IsEditable(row, column)
						? (checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL)
						: (checked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED);
					SIZE glyph{};
					GetThemePartSize(hTheme, nmcd.nmcd.hdc, BP_CHECKBOX, state, nullptr, TS_DRAW, &glyph);
					RECT rcGlyph{
						rc.left + (rc.right - rc.left - glyph.cx) / 2,
						rc.top + (rc.bottom - rc.top - glyph.cy) / 2,
						0, 0,
					};
					rcGlyph.right = rcGlyph.left + glyph.cx;
					rcGlyph.bottom = rcGlyph.top + glyph.cy;
					DrawThemeBackground(hTheme, nmcd.nmcd.hdc, BP_CHECKBOX, state, &rcGlyph, nullptr);
					CloseThemeData(hTheme);
				}
			}

			// The current cell, outlined while the list has the focus.
			if (row == m_row && column == m_column && GetFocus() == m_hList) {
				RECT rcFocus = rc;
				InflateRect(&rcFocus, -1, -1);
				DrawFocusRect(nmcd.nmcd.hdc, &rcFocus);
			}
			return CDRF_DODEFAULT;
		}
	}
	return std::nullopt;
}

void XivAlexander::Apps::MainApp::Window::GridView::UpdateColumnWidths() {
	if (m_columns.empty())
		return;
	RECT rc;
	GetClientRect(m_hList, &rc);
	auto used = 0;
	for (size_t i = 0; i + 1 < m_columns.size(); ++i) {
		const auto width = static_cast<int>(m_columns[i].Width * m_zoom);
		ListView_SetColumnWidth(m_hList, static_cast<int>(i), width);
		used += width;
	}
	ListView_SetColumnWidth(m_hList, static_cast<int>(m_columns.size() - 1), std::max(static_cast<int>(m_columns.back().Width * m_zoom), static_cast<int>(rc.right) - used));
}

void XivAlexander::Apps::MainApp::Window::GridView::ApplyTheme(bool dark) {
	m_dark = dark;
	const auto& colors = GetThemeColors(dark);
	m_backgroundBrush = colors.CreateBackgroundBrush();

	ApplyDarkModeToControl(m_hList, dark);
	const auto hHeader = ListView_GetHeader(m_hList);
	ApplyDarkModeToControl(hHeader, dark);
	SetWindowTheme(hHeader, dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
	ListView_SetBkColor(m_hList, colors.GetBackground());
	ListView_SetTextBkColor(m_hList, colors.GetBackground());
	ListView_SetTextColor(m_hList, colors.GetForeground());
	if (m_editor)
		ApplyDarkModeToControl(m_editor->Window, dark);
	RedrawWindow(m_hList, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void XivAlexander::Apps::MainApp::Window::GridView::SetZoom(double zoom, HFONT hFont) {
	m_zoom = zoom;
	m_hFont = hFont;
	SendMessageW(m_hList, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
	if (m_editor)
		SendMessageW(m_editor->Window, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
	UpdateColumnWidths();
}
