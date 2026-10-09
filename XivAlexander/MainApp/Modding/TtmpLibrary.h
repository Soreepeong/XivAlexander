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

		/// \returns The directories whose contents make up the top level, in scan order.
		[[nodiscard]] std::vector<std::filesystem::path> SearchDirectories() const { return GetPossibleTtmpDirs(); }

		// These change directories on disk and throw std::invalid_argument with a user-facing message if refused; callers must hold the tree lock.
		// Rename and Move also need pack data reads stopped, as released file handles must stay closed until the move; VirtualSqPacks does that.

		void ValidateRename(const NestedTtmp& item, const std::wstring& newName) const;

		/// Keeps the item's place among its siblings. \returns Whether anything changed.
		bool Rename(const std::shared_ptr<NestedTtmp>& item, const std::wstring& newName);

		void ValidateMove(const NestedTtmp& item, const std::filesystem::path& folderDir) const;

		/// folderDir is a folder, a search directory, or an empty directory inside either; the item goes last there. \returns Whether anything changed.
		bool Move(const std::shared_ptr<NestedTtmp>& item, const std::filesystem::path& folderDir);

		/// The new directory shows up in the tree once something is moved into it. \returns Its path.
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

		// Keep order.json in sync with the disk. WriteOrder saves the order in full (top-level children into their search directory's file);
		// AppendLast saves it with item last; ForgetMissingInOrder drops directories that are gone, without creating a file.
		void WriteOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children);
		void AppendLast(const std::shared_ptr<NestedTtmp>& folder, const std::shared_ptr<NestedTtmp>& item);
		void ForgetMissingInOrder(const NestedTtmp& folder) const;
	};
}
