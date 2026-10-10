#pragma once

#include "Utils/Win32/Closeable.h"

namespace XivAlexander::Apps::MainApp::Window {
	/// Spreadsheet-like report list view: arrows move the current cell; F2, Enter, double click or typing edit it (Enter or leaving
	/// applies, Esc cancels, Tab applies and moves on); Space or click toggles checkboxes; drag or Ctrl+Up/Down reorders rows.
	class GridView {
	public:
		enum class CellKind {
			ReadOnly,
			Check,
			Text,
			ComboEdit,  // Text, or one of the choices.
			ComboChoice,  // One of the choices.
		};

		struct Column {
			std::wstring Title;
			CellKind Kind = CellKind::Text;
			int Width = 100;  // At 100% zoom; the last column takes what is left.
			std::function<std::vector<std::wstring>(size_t row)> GetChoices;
		};

		/// Row indices are the owner's.
		struct Source {
			std::function<size_t()> GetRowCount;
			std::function<std::wstring(size_t row, size_t column)> GetText;
			std::function<std::wstring(size_t row, size_t column)> GetEditText;  // What an editor starts with; GetText if none.
			std::function<bool(size_t row, size_t column)> GetChecked;
			std::function<bool(size_t row, size_t column)> IsEditable;  // All cells but read-only columns if none.

			// Return false to refuse; the editor stays, if it can.
			std::function<bool(size_t row, size_t column, const std::wstring& text)> SetText;
			std::function<void(size_t row, size_t column, bool checked)> SetChecked;

			// Moves a row to be before the row now at the index to (the row count for last). Rows can't be moved if none.
			std::function<void(size_t from, size_t to)> MoveRow;

			// A double click on a row, or Enter on a cell that can't be edited; editing the cell if none.
			std::function<void(size_t row)> Activate;

			std::function<void(size_t row, POINT ptScreen)> ShowContextMenu;

			// Called after the list is done with its own notification.
			std::function<void(std::optional<size_t> row)> SelectionChanged;

			// Rows may be selected together, with Ctrl and Shift; a check box of a selected row then sets those of all.
			bool MultiSelect = false;
			// Sets a check box of several rows at once; SetChecked for each if none.
			std::function<void(const std::vector<size_t>& rows, size_t column, bool checked)> SetCheckedRows;

			// A key pressed in the list, not editing; true if handled.
			std::function<bool(UINT vk)> KeyDown;
		};

	private:
		using Brush = Utils::Win32::Brush;

		const std::vector<Column> m_columns;
		const Source m_source;

		HWND m_hWnd{};  // Holds the list.
		HWND m_hList{};
		HFONT m_hFont{};
		double m_zoom = 1;
		bool m_dark = false;
		Brush m_backgroundBrush;

		size_t m_row = 0;
		size_t m_column = 0;

		struct Editor {
			HWND Window{};
			HWND Input{};  // Gets the keys: the window, or the edit of an editable combo box.
			size_t Row{};
			size_t Column{};
		};
		std::optional<Editor> m_editor;
		bool m_endingEdit = false;

		std::optional<size_t> m_dragRow;
		std::optional<size_t> m_dropBefore;  // Where a dragged row would go: before this row, or last if the row count.
		bool m_selectionChangePosted = false;

	public:
		GridView(HWND hParent, UINT id, std::vector<Column> columns, Source source);
		GridView(const GridView&) = delete;
		GridView& operator=(const GridView&) = delete;
		~GridView();

		[[nodiscard]] HWND Handle() const { return m_hWnd; }

		void Refresh();

		[[nodiscard]] std::optional<size_t> GetSelectedRow() const;
		[[nodiscard]] std::vector<size_t> GetSelectedRows() const;
		void Select(size_t row);
		void ClearSelection();
		void BeginEdit(size_t row, size_t column, std::optional<wchar_t> typed = std::nullopt);
		[[nodiscard]] bool IsEditing() const { return m_editor.has_value(); }

		/// Cancels the edit if SetText refuses it.
		void CommitEdit();

		void ApplyTheme(bool dark);
		void SetZoom(double zoom, HFONT hFont);

	private:
		enum class EndEditMode {
			Cancel,
			Apply,  // Keeps editing if refused.
			ApplyOrCancel,  // Cancels if refused: the editor is losing the focus.
		};

		static LRESULT CALLBACK ListSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ref);
		static LRESULT CALLBACK EditorSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ref);
		LRESULT ContainerProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT ListProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT EditorProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		std::optional<LRESULT> OnCustomDraw(NMLVCUSTOMDRAW& nmcd);

		bool EndEdit(EndEditMode mode);
		void MoveCurrent(size_t row, size_t column);
		void Toggle(size_t row, size_t column);
		void MoveRow(size_t from, size_t to);
		void UpdateColumnWidths();
		[[nodiscard]] bool IsEditable(size_t row, size_t column) const;
		[[nodiscard]] RECT GetCellRect(size_t row, size_t column) const;
		[[nodiscard]] std::optional<std::pair<size_t, size_t>> HitTest(POINT ptClient) const;
		[[nodiscard]] size_t GetDropTarget(POINT ptClient) const;
		void EndDrag(bool drop);
		[[nodiscard]] size_t GetRowCount() const { return m_source.GetRowCount ? m_source.GetRowCount() : 0; }
	};
}
