#pragma once

#include <xivres/util.on_dtor.h>

#include "MainApp/Windows/GridView.h"
#include "Utils/Win32/Closeable.h"

namespace XivAlexander {
	struct ToggledPath;
	class Config;
	class ConfigGroup;
	class ConfigItemBase;
	class ConfigNode;
	template<typename T> class ConfigItem;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	struct NestedTtmp;
}

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ConfigWindow;

	/// The status and the runtime configuration as a tree of pages, applied as changed; the host draws the status and runs the pages' commands.
	class SettingsView {
	public:
		/// The next restart's choices, kept by the host, by their menu commands.
		struct RestartChoices {
			std::function<std::pair<bool, bool>(UINT commandId)> GetState;  // Whether it is chosen, and whether it can be changed.
			std::function<void(UINT commandId)> Choose;
		};

	private:
		struct Row;
		using Font = Utils::Win32::Closeable<HFONT, DeleteObject>;

		/// An item without a label is shown with its key.
		struct ItemInfo {
			UINT LabelId = 0;  // A string resource,
			UINT MenuId = 0;  // or the text of a command in a menu resource.
			UINT MenuCommand = 0;
			const wchar_t* FixedLabel = nullptr;  // Or this, the same in every language.
			UINT DescriptionId = 0;  // Shown below it,
			UINT TooltipId = 0;  // or by a "(?)" after its label.
			bool Hidden = false;
			bool VersionSensitive = false;

			// For a group: shown on its parent's page under a heading, instead of in the tree.
			bool Inline = false;
			// For a group: its heading is this item of it, a check box, instead of its label.
			const ConfigItemBase* HeadingItem = nullptr;
			// For a group: its check boxes without descriptions side by side, as many as fit on a line.
			bool Flow = false;
			// For an item: shown at the end of its page, in a tab of its own among the page's others.
			bool Tabbed = false;
			// For an item: shown after its group's inline groups, instead of before them.
			bool AfterInlineGroups = false;
			// For a number after a check box: on the check box's line, after it; its label is the unit after the box.
			bool BesideCheck = false;
			// For a group: (label string resource, or 0 for the menu text; command) buttons atop its page, sending commands to the owner.
			std::vector<std::pair<UINT, UINT>> Actions;
			// For a group: the items of another node, shown at the end of its page under a heading.
			const ConfigNode* AppendNode = nullptr;
			UINT AppendHeadingId = 0;
			UINT AppendDescriptionId = 0;
			// For an item: a note before it, about it and the items after it.
			UINT NoteId = 0;
			// For a check box: what it is doing now, at the end of its line in the weak color; asked again every second.
			std::function<std::wstring()> Status;

			// For a list of paths: of files of this kind, rather than folders.
			enum class FileKind : uint8_t { None, Dll, Json } PickFiles = FileKind::None;
		};

		App& m_app;
		const std::shared_ptr<Config> m_config;

		// Receives the commands the pages send.
		const HWND m_hWndOwner;
		const std::function<double()> m_getZoom;
		const std::function<bool()> m_isDarkMode;
		const std::function<void(HDC, const RECT&)> m_paintStatus;
		const RestartChoices m_restartChoices;

		HWND m_hWnd{};  // Holds the tree, the page, and the status.
		std::optional<int> m_dividerDragOffset;  // From where the tree ends to where the divider is held, while dragged.
		std::optional<int> m_draggedTreeWidth;  // In pixels at 96 DPI; kept in the configuration once let go.
		HWND m_hStatus{};

		HWND m_hTree{};
		HWND m_hPage{};
		HWND m_hTooltip{};  // Shared by the rows' "(?)" buttons: on hover, and tracked while one is clicked or focused.
		HWND m_trackedHelp{};  // The "(?)" whose tooltip is tracked.
		bool m_populatingTree = false;
		Font m_font;
		Font m_boldFont;
		Font m_strikeFont;  // Of what is turned off in the tree.
		double m_fontZoom = 0;
		Utils::Win32::Brush m_backgroundBrush;

		std::map<const ConfigItemBase*, ItemInfo> m_itemInfo;

		struct TreeNode {
			enum class NodeKind {
				Groups,  // The items of its groups.
				Status,
				PatchCodes,
				Restart,  // The restart buttons, the items of its groups, and the sessions.
				FramerateLocking,  // Its group, the lock framerate settings, made easier to set.
				FontFamily,  // The sources of a game font family.
				TtmpFolder,  // The TexTools ModPacks in a folder.
				TtmpPack,  // A TexTools ModPack, and its options.
			};

			NodeKind Kind = NodeKind::Groups;

			// Labelled by a string resource, else the top-level menu at this position of the menu resource, else as its first group.
			UINT LabelId = 0;
			int MenuPosition = -1;

			// Shown on its page, each under its heading if there are headings. Without any, its page is its first child's.
			std::vector<const ConfigGroup*> Groups;
			bool Headings = false;

			// With one group, the groups under it come first, unless they are inline or among these.
			std::vector<TreeNode> Children;

			std::string Family;

			// For a folder of TexTools ModPacks, empty for those in none; or for a ModPack, its folder.
			std::filesystem::path TtmpPath;

			// Rows of its own, before and after those of its groups.
			std::function<void()> AddFirst;
			std::function<void()> AddLast;
		};
		std::vector<TreeNode> m_tree;

		struct TtmpPage;
		bool m_listeningToTtmps = false;
		std::vector<std::filesystem::path> m_ttmpNodePaths;  // Of the folders and the ModPacks in the tree.
		std::set<std::filesystem::path> m_ttmpDisabled;  // Of those, the ones turned off.
		std::vector<std::filesystem::path> m_ttmpPagePacks;  // Shown on the page, to tell whether it needs building anew.
		std::shared_ptr<TtmpPage> m_ttmpPage;
		std::wstring m_ttmpFilter;
		std::filesystem::path m_ttmpPackToSelect;
		std::filesystem::path m_ttmpToReselect;  // Where what was shown went, by a rename or a move.
		bool m_ttmpRenameAfterSelect = false;  // A ModPack chosen in a folder's list, to be renamed in the tree.
		bool m_ttmpRenameAfterReselect = false;  // A folder made here, to be named.
		std::vector<std::string> m_fontFamilies;
		// Key is the English name, as the sources keep it; Display is in the UI's language, made unique with the key where needed.
		struct SystemFontFamily {
			std::wstring Key;
			std::wstring Display;
		};
		std::vector<SystemFontFamily> m_systemFontFamilies;
		int m_systemFontFamiliesLanguage = -1;  // The Ui.Language they were read for; -1 before read.
		std::filesystem::path m_fontPresetFolder;  // FontReplacement::Presets::Folder, found when first needed.

		// Region and version of the additional game folders, detected on threads that post WmRefreshRows when done; kept per resolved path.
		struct GameDirectoryDetection;
		std::shared_ptr<GameDirectoryDetection> m_gameDirectories;

		// Name and description of the text transformation files, read when their page is built.
		struct TextTransformationInfo {
			std::wstring Name;
			std::wstring Description;
		};
		std::map<std::filesystem::path, TextTransformationInfo> m_textTransformationInfo;

		// The node whose page is shown.
		const TreeNode* m_pNode{};
		std::vector<std::unique_ptr<ConfigWindow>> m_patchCodeEditors;
		std::vector<std::unique_ptr<Row>> m_rows;
		std::vector<ConfigItemBase*> m_deferredTabbed;  // Met while adding the page's rows, added in tabs after them.
		std::map<const ConfigItemBase*, int> m_activeTabs;  // By the first item of the tabs.
		xivres::util::on_dtor::multi m_rowCleanup;
		int m_scrollY = 0;
		int m_pageFreezes = 0;  // Nested FreezePage guards alive; the page draws again when the last goes.

		// The global cooldowns the game has reported since start, latest first: each its duration and its drift.
		std::deque<std::pair<uint64_t, int64_t>> m_cooldownHistory;
		xivres::util::on_dtor m_cooldownListener;
		xivres::util::on_dtor m_crowdFixListener;  // While the page shows the statuses of CrowdFix.
		int m_contentHeight = 0;

		xivres::util::on_dtor::multi m_cleanup;

	public:
		SettingsView(App& app, HWND hWndParent, HWND hWndOwner, std::function<double()> getZoom, std::function<bool()> isDarkMode, std::function<void(HDC, const RECT&)> paintStatus, RestartChoices restartChoices);
		SettingsView(const SettingsView&) = delete;
		SettingsView& operator=(const SettingsView&) = delete;
		~SettingsView();

		[[nodiscard]] HWND Handle() const { return m_hWnd; }

		/// Deferred past the message being handled: a change made on a page comes during its control's notification, and the page is rebuilt.
		void ApplyLanguage();
		void ApplyTheme();

		/// Applies what is being typed; for Enter.
		void CommitTypedText();

		void InvalidateStatus() const;

	private:
		/// Asks the rows with an ItemInfo::Status for it again, laying the page out anew if any changed.
		void RefreshRowStatuses();

		void InitializeItemInfo();

		LRESULT PageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		void OnPageCommand(int controlId, int notification);
		std::optional<LRESULT> OnControlColor(UINT uMsg, HDC hdc, HWND hControl);

		void PopulateTree();
		void ExpandTree(std::vector<TreeNode>& nodes) const;
		[[nodiscard]] std::wstring GetNodeLabel(const TreeNode& node) const;
		void ClearRows();
		/// Stops the page drawing until the returned guard and any others alive go; then redraws it whole.
		[[nodiscard]] xivres::util::on_dtor FreezePage();
		void ShowPage(const TreeNode* pNode);
		// Again whenever CrowdFix is made anew: its statuses change on game threads.
		void ListenToCrowdFix();
		void AddGroupSection(const ConfigGroup& group, bool heading);
		void AddGroupRows(const ConfigNode& node);
		void AddPatchCodeRows();
		void AddRestartRows();
		void AddSessionRows();
		void BeginSessionRename(Row& row, const std::string& alias);
		void AddFramerateLockingRows();
		void CreateValueRow(const std::wstring& label, std::function<std::wstring()> getText);
		void CreateNumberRow(const std::wstring& label, std::function<std::wstring()> getText, std::function<void(const std::wstring&)> setText);
		void CreateComboRow(const std::wstring& label, const std::vector<std::wstring>& choices, std::function<int()> getChoice, std::function<void(int)> setChoice);
		void CreateListRow(const std::wstring& label, const std::wstring& buttonLabel, std::function<std::vector<std::wstring>()> getItems, std::function<void()> onButton);
		void CreateChoiceRow(const std::wstring& label, bool enabled, std::function<bool()> isChosen, std::function<void()> choose, bool checkBox = false, int flowGroup = 0);
		void CreateRow(ConfigItemBase& item);
		void CreateHeadingRow(const std::wstring& text);
		void CreateHeadingRow(const std::wstring& text, const std::wstring& checkLabel, bool enabled, std::function<bool()> isChecked, std::function<void()> toggle,
			const std::wstring& linkLabel = {}, std::function<void()> link = {});
		void CreateNoteRow(const std::wstring& text);
		// A "(?)" after the label of the row at the index, showing the text.
		void AddHelp(size_t rowIndex, const std::wstring& text);
		void AddHelp(size_t rowIndex, UINT tooltipId);
		void ShowHelp(HWND hHelp, bool show);
		void AddTabbedRows();
		void CreateRichTextRow(const std::wstring& text);
		void CreateEnabledRow(std::function<bool()> isChecked, std::function<void()> toggle, std::vector<std::pair<std::wstring, std::function<void()>>> actions);
		void CreateActionRow(std::vector<std::pair<std::wstring, std::function<void()>>> buttons);
		GridView& CreateGridRow(const std::wstring& label, UINT descriptionId, int height, std::vector<GridView::Column> columns, GridView::Source source);
		// Labeled as item, whose changes refresh the rows.
		GridView& CreateListGridRow(ConfigItemBase& item, UINT descriptionId, int height, std::vector<GridView::Column> columns, GridView::Source source);
		// Posts the message to the view whenever source changes, until the page's rows go.
		template<typename T> void PostOnChange(T& source, UINT message);
		void CreateSliderRow(const std::wstring& label, double minimum, double maximum, double step, std::function<double()> getValue, std::function<void(double)> setValue);
		bool CreateSpecialRows(const ConfigItemBase& item);
		void AddPathReplacementRows();
		void AddPathFilterRows();
		void AddFallbackPriorityRows();
		void AddForcedLanguageRows();
		void AddChoicesFileRows();
		void AddDirectoryListRows(ConfigItem<std::vector<ToggledPath>>& item, std::vector<ToggledPath> defaults, UINT descriptionId);
		void AddSqpackRootRows();
		void AddTextTransformationRows();
		void AddEdgeRows();
		void AddSubpixelRows();
		void AddFontFamilyRows(const std::string& family);
		[[nodiscard]] RECT GetDividerRect() const;
		void DragWindow() const;
		void ListenToTtmps();
		bool AttachTtmpNodes(bool force);
		void SelectNode(const std::function<bool(const TreeNode&)>& predicate);
		void OnTtmpSetsChanged();
		void AddTtmpFolderRows(const std::filesystem::path& folder);
		void FilterTtmps();
		void AddTtmpPackRows(const std::filesystem::path& path);
		void AddTtmpProfileRow();
		[[nodiscard]] std::shared_ptr<Features::Modding::NestedTtmp> FindTtmp(const std::filesystem::path& path) const;
		bool RunTtmpOperation(const std::function<void()>& operation, std::filesystem::path reselect = {});
		void ShowTtmpMoveMenu(std::vector<std::shared_ptr<Features::Modding::NestedTtmp>> items);
		void MoveTtmpToNewFolder(const std::vector<std::shared_ptr<Features::Modding::NestedTtmp>>& items);
		void RenameTtmpInTree(const std::filesystem::path& path);
		void RenameShownTtmp();
		void AddTtmpDetailRows(const std::shared_ptr<Features::Modding::NestedTtmp>& pack);
		void SetTtmpEnabled(Features::Modding::NestedTtmp& nestedTtmp, bool enabled);
		void ChooseTtmpOption(const std::shared_ptr<Features::Modding::NestedTtmp>& pack, size_t pageIndex, size_t groupIndex, size_t optionIndex, bool multiple);
		void DeleteTtmps(const std::vector<std::shared_ptr<Features::Modding::NestedTtmp>>& packs);
		void TruncateRows(size_t count);
		const std::filesystem::path& GetFontPresetFolder();
		[[nodiscard]] std::vector<std::wstring> ListPresets();
		/// For a file outside the presets folder, asks whether to import it or use it in place; returns FontReplacementFamilySource::Preset's value.
		std::optional<std::string> ChooseFontPreset();
		const std::vector<SystemFontFamily>& ListSystemFontFamilies();
		// The name shown for a kept one.
		[[nodiscard]] std::wstring SystemFontDisplayName(const std::string& key);
		// The name to keep for one typed or chosen; as typed if not installed.
		[[nodiscard]] std::wstring SystemFontKey(const std::wstring& typed);
		bool RenamePatchCode(const std::filesystem::path& path, const std::wstring& text);
		bool RenamePatchCodeName(const std::filesystem::path& path, const std::string& digest, const std::wstring& text);
		void DeletePatchCode(const std::string& digest, const std::filesystem::path& path);
		void OpenPatchCodeEditor(const std::wstring& name, const std::filesystem::path& path);
		void CreatePatchCode();
		// For the row being created, or the one at rowIndex.
		HWND CreateRowControl(int part, DWORD exStyle, LPCWSTR className, const std::wstring& text, DWORD style, std::optional<size_t> rowIndex = std::nullopt);
		void AddDescription(size_t rowIndex, UINT descriptionId);
		void RefreshRows();
		void LayoutPage();
		void ScrollPageTo(int y);
		void ScrollIntoView(HWND hControl);

		LRESULT ContainerProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT StatusProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		void Layout();
		void RefreshTheme();

		[[nodiscard]] double GetZoom() const { return m_getZoom(); }
		[[nodiscard]] bool IsDarkModeEnabled() const { return m_isDarkMode(); }

		void UpdateFont(double zoom);
		void ApplyThemeToControl(HWND hControl) const;

		[[nodiscard]] const ItemInfo* FindItemInfo(const ConfigItemBase& item) const;
		[[nodiscard]] std::wstring GetMenuText(UINT menuId, UINT commandId) const;
		[[nodiscard]] std::wstring GetTopLevelMenuText(int position) const;
		[[nodiscard]] std::wstring GetItemLabel(const ConfigItemBase& item) const;
	};
}
