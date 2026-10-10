#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MainApp/Modding/NestedTtmp.h"

namespace XivAlexander {
	class Config;
}

namespace XivAlexander::Misc {
	class Logger;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class TtmpLibrary {
		const std::shared_ptr<Config> m_config;
		const std::shared_ptr<Misc::Logger> m_logger;
		const std::filesystem::path m_sqpackPath;
		std::shared_ptr<NestedTtmp> m_root;

	public:
		using Reservations = std::map<std::string, std::vector<std::pair<xivres::path_spec, uint32_t>>>;

		explicit TtmpLibrary(std::filesystem::path sqpackPath);
		~TtmpLibrary();

		[[nodiscard]] const std::shared_ptr<NestedTtmp>& Root() const { return m_root; }

		void Scan(Window::ProgressPopupWindow& progressWindow);
		void Rescan(Window::ProgressPopupWindow& progressWindow);
		[[nodiscard]] Reservations CollectReservations(Window::ProgressPopupWindow& progressWindow) const;

		NestedTtmp* Add(const std::filesystem::path& ttmplPath, Window::ProgressPopupWindow& progressWindow);
		void Delete(const std::filesystem::path& ttmplPath);
		void ReconcileFiles();
		void SaveChoices(NestedTtmp& ttmp) const;
		void ReloadChoices();

		/// \returns The directories whose contents make up the top level of the library, in the order they are scanned.
		[[nodiscard]] std::vector<std::filesystem::path> SearchDirectories() const { return GetPossibleTtmpDirs(); }

		// The functions below change directories on disk, and throw std::invalid_argument with a message fit to show
		// when asked for something that cannot be done. Callers must hold the tree lock. Rename and Move also need every
		// read from the packs' data files stopped, as they release the file handles and must not see them opened again
		// until the directory has moved; VirtualSqPacks does that, and reapplies the packs afterwards.

		void ValidateRename(const NestedTtmp& item, const std::wstring& newName) const;

		/// Renames the directory of a pack or folder, keeping it where it is among its siblings.
		/// \returns Whether anything changed.
		bool Rename(const std::shared_ptr<NestedTtmp>& item, const std::wstring& newName);

		void ValidateMove(const NestedTtmp& item, const std::filesystem::path& folderDir) const;

		/// Moves the directory of a pack or folder into folderDir, which is a folder, a search directory, or an empty
		/// directory inside either. The item goes last among its new siblings, as if newly added.
		/// \returns Whether anything changed.
		bool Move(const std::shared_ptr<NestedTtmp>& item, const std::filesystem::path& folderDir);

		/// Creates an empty directory, which shows up in the tree once something is moved into it.
		/// \returns Path of the new directory.
		std::filesystem::path CreateFolder(const std::filesystem::path& parentDir, const std::wstring& name) const;

		void ValidateOrder(const NestedTtmp& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children) const;

		/// Saves the order of the children of a folder (or of the root) to order.json, and sorts them accordingly.
		void SetOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children);

	private:
		struct FolderChain {
			std::filesystem::path Root;
			/// Directories between Root and the folder, the folder first; empty if the folder is Root.
			std::vector<std::filesystem::path> Dirs;
		};

		[[nodiscard]] std::vector<std::filesystem::path> GetPossibleTtmpDirs() const;
		[[nodiscard]] std::string ResolveChoicesFileName() const;
		[[nodiscard]] bool IsDisabled(const std::filesystem::path& dir) const;

		void RescanTree(const std::filesystem::path& path, std::shared_ptr<NestedTtmp> parent, Window::ProgressPopupWindow& progressWindow);
		std::shared_ptr<NestedTtmp> AddFromTtmpl(const std::filesystem::path& ttmplPath, const std::shared_ptr<NestedTtmp>& parent);
		std::shared_ptr<NestedTtmp> FindContainer(const std::filesystem::path& ttmpl, bool create);
		std::shared_ptr<NestedTtmp> FindFolder(const std::filesystem::path& dir, bool create);
		[[nodiscard]] std::optional<FolderChain> ResolveFolderChain(const std::filesystem::path& dir) const;
		[[nodiscard]] bool Contains(const NestedTtmp& item) const;
		[[nodiscard]] uint64_t LookupOrderIndex(const NestedTtmp& folder, const std::filesystem::path& name) const;
		void Relocate(NestedTtmp& item, const std::filesystem::path& newPath);

		// Keeps order.json true to the disk after a change to it. WriteOrder saves children's order in full (each top-level
		// child's in the file of the search directory it is in); AppendLast saves the folder's order with item last, so
		// that it comes after every sibling; ForgetMissingInOrder drops the names of directories that are gone, without
		// making a file where there was none.
		void WriteOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children);
		void AppendLast(const std::shared_ptr<NestedTtmp>& folder, const std::shared_ptr<NestedTtmp>& item);
		void ForgetMissingInOrder(const NestedTtmp& folder) const;
	};
}
