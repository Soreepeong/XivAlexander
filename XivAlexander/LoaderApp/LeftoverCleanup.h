#pragma once

#include <xivres/common.h>

// Leftovers after the last uninstall; only the two XivAlexander folders are touched, and links are never followed.
namespace XivAlexander::LoaderApp::LeftoverCleanup {
	enum Item : uint32_t {
		Caches = 1 << 0,  // <config>\Cached
		ProgramFiles = 1 << 1,  // the program folder
		Sessions = 1 << 2,  // login sessions in Credential Manager
		Settings = 1 << 3,  // files directly in <config>
		UserContent = 1 << 4,  // folders in <config> other than Cached
	};

	struct Scope {
		std::filesystem::path ConfigDir;
		std::filesystem::path ProgramDir;

		/// False when other regions are still installed: then only that region's sessions are offered.
		bool LastInstallation = true;

		/// The region whose sessions go; every region if unset.
		std::optional<xivres::game_release_publisher> SessionsPublisher;

		bool IncludeUnknownSessions = true;

		/// Shows what would be removed and removes nothing.
		bool TestOnly = false;

		[[nodiscard]] uint32_t OfferedItems() const;
	};

	struct Result {
		size_t SessionsRemoved = 0;
		size_t EntriesLeft = 0;  // in use or not removable
		std::wstring Error;
	};

	[[nodiscard]] size_t CountSessions(const Scope& scope);
	[[nodiscard]] uint64_t MeasureItem(const Scope& scope, Item item);

	/// Needs COM on the calling thread for the Recycle Bin.
	Result Perform(const Scope& scope, uint32_t items);

	/// The items the user chose, or 0 if cancelled.
	uint32_t ShowDialog(HWND hParent, WORD langId, bool dark, const Scope& scope);
}
