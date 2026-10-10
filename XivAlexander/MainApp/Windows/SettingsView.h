#pragma once

#include <xivres/util.on_dtor.h>

#include "MainApp/Windows/GridView.h"
#include "Utils/Win32/Closeable.h"

namespace XivAlexander {
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

	/// The status, and all of the runtime configuration: a tree of pages, of which the configuration's are of its groups,
	/// with their items, each applied as it is changed. A child window of its host, which draws the status and runs the
	/// commands the pages send.
	class SettingsView {
	public:
		/// The choices for the next restart that the host keeps, by their menu commands.
		struct RestartChoices {
			std::function<std::pair<bool, bool>(UINT commandId)> GetState;  // Whether it is chosen, and whether it can be changed.
			std::function<void(UINT commandId)> Choose;
		};

	private:
		struct Row;
		using Font = Utils::Win32::Closeable<HFONT, DeleteObject>;

		/// How an item is shown. An item without a label is shown with its key.
		struct ItemInfo {
			UINT LabelId = 0;  // A string resource,
			UINT MenuId = 0;  // or the text of a command in a menu resource.
			UINT MenuCommand = 0;
			UINT DescriptionId = 0;
			bool Hidden = false;
			bool VersionSensitive = false;

			// For a group: shown on its parent's page under a heading, instead of in the tree.
			bool Inline = false;
			// For a group: buttons atop its page, sending commands to the owner; labelled by a string resource, or with
			// the command's menu text if it is 0.
			std::vector<std::pair<UINT, UINT>> Actions;
			// For a group: the items of another node, shown at the end of its page under a heading.
			const ConfigNode* AppendNode = nullptr;
			UINT AppendHeadingId = 0;

			// For a list of paths: of files, rather than folders.
			bool PickFiles = false;
		};

		App& m_app;
		const std::shared_ptr<Config> m_config;

		// Receives the commands the pages send, such as opening the runtime configuration editor.
		const HWND m_hWndOwner;
		const std::function<double()> m_getZoom;
		const std::function<bool()> m_isDarkMode;
		const std::function<void(HDC, const RECT&)> m_paintStatus;
		const RestartChoices m_restartChoices;
		const std::function<void(Features::Modding::NestedTtmp& folder, UINT commandId)> m_batchTtmp;  // Enable, disable, or delete all.

		HWND m_hWnd{};  // Holds the tree, the page, and the status.
		std::optional<int> m_dividerDragOffset;  // From where the tree ends to where the divider is held, while dragged.
		std::optional<int> m_draggedTreeWidth;  // In pixels at 96 DPI; kept in the configuration once let go.
		HWND m_hStatus{};

		HWND m_hTree{};
		HWND m_hPage{};
		bool m_populatingTree = false;
		Font m_font;
		Font m_boldFont;
		Font m_strikeFont;  // Of what is turned off in the tree.
		double m_fontZoom = 0;
		Utils::Win32::Brush m_backgroundBrush;

		std::map<const ConfigItemBase*, ItemInfo> m_itemInfo;

		/// A node of the tree, and its page.
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

			// Labelled by a string resource, or the top-level menu at this position of the menu resource, or else as its
			// first group.
			UINT LabelId = 0;
			int MenuPosition = -1;

			// Shown on its page, each under its heading if there are headings. Without any, its page is its first child's.
			std::vector<const ConfigGroup*> Groups;
			bool Headings = false;

			// With one group, the groups under it come first, unless they are inline or among these.
			std::vector<TreeNode> Children;

			// For a font family's page.
			std::string Family;

			// For a folder of TexTools ModPacks, empty for those in none; or for a ModPack, its folder.
			std::filesystem::path TtmpPath;

			// Rows of its own, before and after those of its groups.
			std::function<void()> AddFirst;
			std::function<void()> AddLast;
		};
		std::vector<TreeNode> m_tree;

		// The TexTools ModPacks: their folders in the tree, and the page of a folder.
		struct TtmpPage;
		bool m_listeningToTtmps = false;
		std::vector<std::filesystem::path> m_ttmpNodePaths;  // Of the folders and the ModPacks in the tree.
		std::set<std::filesystem::path> m_ttmpDisabled;  // Of those, the ones turned off.
		std::vector<std::filesystem::path> m_ttmpPagePacks;  // Shown on the page, to tell whether it needs building anew.
		std::shared_ptr<TtmpPage> m_ttmpPage;
		std::wstring m_ttmpFilter;
		std::filesystem::path m_ttmpPackToSelect;
		std::filesystem::path m_ttmpToReselect;  // Where what was shown went, by a rename or a move.
		bool m_ttmpRenameAfterReselect = false;  // A folder made here, to be named.
		std::vector<std::string> m_fontFamilies;
		std::vector<std::wstring> m_systemFontFamilies;

		// The node whose page is shown.
		const TreeNode* m_pNode{};
		std::vector<std::unique_ptr<ConfigWindow>> m_patchCodeEditors;
		std::vector<std::unique_ptr<Row>> m_rows;
		xivres::util::on_dtor::multi m_rowCleanup;
		int m_scrollY = 0;

		// The global cooldowns the game has reported since start, latest first: each its duration and its drift.
		std::deque<std::pair<uint64_t, int64_t>> m_cooldownHistory;
		xivres::util::on_dtor m_cooldownListener;
		int m_contentHeight = 0;

		xivres::util::on_dtor::multi m_cleanup;

	public:
		SettingsView(App& app, HWND hWndParent, HWND hWndOwner, std::function<double()> getZoom, std::function<bool()> isDarkMode, std::function<void(HDC, const RECT&)> paintStatus, RestartChoices restartChoices, std::function<void(Features::Modding::NestedTtmp& folder, UINT commandId)> batchTtmp);
		SettingsView(const SettingsView&) = delete;
		SettingsView& operator=(const SettingsView&) = delete;
		~SettingsView();

		[[nodiscard]] HWND Handle() const { return m_hWnd; }

		/// Labels and colors anew, after the message being handled: a change made on a page comes during its control's
		/// notification, and the page is made anew.
		void ApplyLanguage();
		void ApplyTheme();

		/// Applies what is being typed; for Enter.
		void CommitTypedText();

		/// Draws the status anew, if it is shown.
		void InvalidateStatus() const;

	private:
		void InitializeItemInfo();

		LRESULT PageProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		void OnPageCommand(int controlId, int notification);
		std::optional<LRESULT> OnControlColor(UINT uMsg, HDC hdc, HWND hControl);

		void PopulateTree();
		void ExpandTree(std::vector<TreeNode>& nodes) const;
		[[nodiscard]] std::wstring GetNodeLabel(const TreeNode& node) const;
		void ClearRows();
		void ShowPage(const TreeNode* pNode);
		void AddGroupSection(const ConfigGroup& group, bool heading);
		void AddGroupRows(const ConfigNode& node);
		void AddPatchCodeRows();
		void AddRestartRows();
		void AddSessionRows();
		void AddFramerateLockingRows();
		void CreateValueRow(const std::wstring& label, std::function<std::wstring()> getText);
		void CreateNumberRow(const std::wstring& label, std::function<std::wstring()> getText, std::function<void(const std::wstring&)> setText);
		void CreateComboRow(const std::wstring& label, const std::vector<std::wstring>& choices, std::function<int()> getChoice, std::function<void(int)> setChoice);
		void CreateListRow(const std::wstring& label, const std::wstring& buttonLabel, std::function<std::vector<std::wstring>()> getItems, std::function<void()> onButton);
		void CreateChoiceRow(const std::wstring& label, bool enabled, std::function<bool()> isChosen, std::function<void()> choose, bool checkBox = false, int flowGroup = 0);
		void CreateRow(ConfigItemBase& item);
		void CreateHeadingRow(const std::wstring& text);
		void CreateNoteRow(const std::wstring& text);
		void CreateActionRow(std::wstring label1, std::function<void()> onClick1, std::wstring label2 = {}, std::function<void()> onClick2 = {});
		void CreateActionRow(std::vector<std::pair<std::wstring, std::function<void()>>> buttons);
		GridView& CreateGridRow(const std::wstring& label, UINT descriptionId, int height, std::vector<GridView::Column> columns, GridView::Source source);
		void CreateSliderRow(const std::wstring& label, double minimum, double maximum, double step, std::function<double()> getValue, std::function<void(double)> setValue);
		bool CreateSpecialRows(const ConfigItemBase& item);
		void AddPathReplacementRows();
		void AddPathFilterRows();
		void AddFallbackPriorityRows();
		void AddForcedLanguageRows();
		void AddChoicesFileRows();
		void AddDirectoryListRows(ConfigItem<std::vector<std::filesystem::path>>& item, std::vector<std::filesystem::path> defaults, UINT descriptionId);
		void AddEdgeRows();
		void AddFontFamilyRows(const std::string& family);
		[[nodiscard]] RECT GetDividerRect() const;
		void ListenToTtmps();
		bool AttachTtmpNodes(bool force);
		void SelectNode(const std::function<bool(const TreeNode&)>& predicate);
		void OnTtmpSetsChanged();
		void AddTtmpFolderRows(const std::filesystem::path& folder);
		void FilterTtmps();
		void AddTtmpPackRows(const std::filesystem::path& path);
		[[nodiscard]] std::shared_ptr<Features::Modding::NestedTtmp> FindTtmp(const std::filesystem::path& path) const;
		bool RunTtmpOperation(const std::function<void()>& operation, std::filesystem::path reselect = {});
		void ShowTtmpMoveMenu(std::vector<std::shared_ptr<Features::Modding::NestedTtmp>> items);
		void MoveTtmpToNewFolder(const std::shared_ptr<Features::Modding::NestedTtmp>& item);
		void RenameShownTtmp();
		void AddTtmpDetailRows(const std::shared_ptr<Features::Modding::NestedTtmp>& pack);
		void SetTtmpEnabled(Features::Modding::NestedTtmp& nestedTtmp, bool enabled);
		void ChooseTtmpOption(const std::shared_ptr<Features::Modding::NestedTtmp>& pack, size_t pageIndex, size_t groupIndex, size_t optionIndex, bool multiple);
		void DeleteTtmp(const std::shared_ptr<Features::Modding::NestedTtmp>& pack);
		void TruncateRows(size_t count);
		[[nodiscard]] std::vector<std::wstring> ListPresets() const;
		const std::vector<std::wstring>& ListSystemFontFamilies();
		bool RenamePatchCode(const std::filesystem::path& path, const std::wstring& text);
		bool RenamePatchCodeName(const std::filesystem::path& path, const std::string& digest, const std::wstring& text);
		void DeletePatchCode(const std::string& digest, const std::filesystem::path& path);
		void OpenPatchCodeEditor(const std::wstring& name, const std::filesystem::path& path);
		void CreatePatchCode();
		HWND CreateRowControl(int part, DWORD exStyle, LPCWSTR className, const std::wstring& text, DWORD style);
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
