#pragma once

#include <mutex>

#include <xivres/stream.h>
#include <xivres/sqpack.h>
#include <xivres/textools.h>
#include "MainApp/Modding/NestedTtmp.h"

#include <xivres/util.listener_manager.h>

namespace XivAlexander::Apps::MainApp {
	class App;
}

namespace XivAlexander::Apps::MainApp::Window {
	class ProgressPopupWindow;
}

namespace XivAlexander::Apps::MainApp::Features::Modding {
	class SqpackRebuildLock;

	class VirtualSqPacks {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		VirtualSqPacks(App& App, std::filesystem::path sqpackPath, SqpackRebuildLock& ioGate);
		~VirtualSqPacks();

		std::shared_ptr<xivres::stream> OpenStream(const std::filesystem::path& path);

		bool EntryExists(const xivres::path_spec& pathSpec) const;
		[[nodiscard]] std::string DescribeEntrySource(const xivres::path_spec& pathSpec) const;
		std::shared_ptr<xivres::stream> GetOriginalEntry(const xivres::path_spec& pathSpec) const;
		std::string FindFutureReservationFor(const xivres::path_spec& pathSpec) const;

		std::string ReserveCrossSqpack(const xivres::path_spec& requested, const xivres::path_spec& target) const;

		std::shared_ptr<NestedTtmp> GetTtmps() const;

		[[nodiscard]] std::unique_lock<std::recursive_mutex> LockTtmps() const;

		void AddNewTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately, Window::ProgressPopupWindow& progressWindow);
		void DeleteTtmp(const std::filesystem::path& ttmpl, bool reflectImmediately = true);
		void RescanTtmp(Window::ProgressPopupWindow& progressWindow);
		void ApplyTtmpChanges(NestedTtmp& nestedTtmp, bool announce = true);

		// Organizing the library. Call these without holding LockTtmps, and not from the game's main thread: those that
		// change anything pause the game and stop sqpack reads while they move directories and reapply the packs, then
		// fire OnTtmpSetsChanged. Nodes stay the same objects, but their Path and those of everything inside change.
		// Each throws std::invalid_argument, with a message fit to show, when asked for something that cannot be done,
		// and std::runtime_error when the file system refuses, in which case the item stays where it was.

		/// \returns The directories whose contents make up the top level of GetTtmps(), in the order they are scanned.
		[[nodiscard]] std::vector<std::filesystem::path> GetTtmpSearchDirectories() const;

		/// Renames the directory of a pack or folder, keeping its place among its siblings.
		void RenameTtmp(const std::shared_ptr<NestedTtmp>& item, const std::wstring& newName);

		/// Moves packs or folders into folderDir: the Path of a folder, one of GetTtmpSearchDirectories(), or an empty
		/// directory inside either, such as one from CreateTtmpFolder. Moved items go last among their new siblings,
		/// which gives them the highest priority there, as newly added packs get. Items inside other listed items move
		/// along with those. Items are moved one by one; if one fails, those before it stay moved.
		void MoveTtmps(const std::vector<std::shared_ptr<NestedTtmp>>& items, const std::filesystem::path& folderDir);

		/// Creates a folder named name in parentDir (as for MoveTtmps), and moves items into it. An empty folder stays
		/// on disk but does not appear in GetTtmps() until something is moved into it, as scanning skips empty folders.
		/// If nothing could be moved in, the new folder is removed again.
		/// \returns Path of the new folder.
		std::filesystem::path CreateTtmpFolder(const std::filesystem::path& parentDir, const std::wstring& name, const std::vector<std::shared_ptr<NestedTtmp>>& items = {});

		/// Reorders the children of a folder, or of GetTtmps() itself, and saves the order to order.json. Packs apply
		/// in tree order, so a later one wins over an earlier one where both replace the same file.
		/// \param children Every child of folder exactly once, in the new order.
		void SetTtmpOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children);

		xivres::util::listener_manager<Implementation, void> OnTtmpSetsChanged;
	};
}
