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

		// Call without LockTtmps, off the game's main thread; changes pause the game and fire OnTtmpSetsChanged. Nodes stay the same objects, but their Paths change.
		// Throw std::invalid_argument (message fit to show) for impossible requests, std::runtime_error when the file system refuses (the item stays put).

		/// \returns The directories making up the top level of GetTtmps(), in scan order.
		[[nodiscard]] std::vector<std::filesystem::path> GetTtmpSearchDirectories() const;

		/// Keeps the item's place among its siblings.
		void RenameTtmp(const std::shared_ptr<NestedTtmp>& item, const std::wstring& newName);

		/// folderDir: a folder's Path, a search directory, or an empty directory in either. Moved items go last (highest priority);
		/// items inside other listed items move along with those. Moves one by one; if one fails, those before it stay moved.
		void MoveTtmps(const std::vector<std::shared_ptr<NestedTtmp>>& items, const std::filesystem::path& folderDir);

		/// parentDir as for MoveTtmps. Scanning skips empty folders, so one stays out of GetTtmps() until filled; removed again if nothing could be moved in.
		std::filesystem::path CreateTtmpFolder(const std::filesystem::path& parentDir, const std::wstring& name, const std::vector<std::shared_ptr<NestedTtmp>>& items = {});

		/// Saves the order to order.json; packs apply in tree order, so a later one wins where both replace the same file.
		/// \param children Every child of folder exactly once.
		void SetTtmpOrder(const std::shared_ptr<NestedTtmp>& folder, const std::vector<std::shared_ptr<NestedTtmp>>& children);

		xivres::util::listener_manager<Implementation, void> OnTtmpSetsChanged;
	};
}
