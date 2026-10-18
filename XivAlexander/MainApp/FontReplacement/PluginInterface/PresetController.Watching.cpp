#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/PresetController.h"

#include <FontChanger.Presets/GlyphFiles.h>

#include "Utils/Win32/Closeable.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Editors write a file in several steps; a change is acted on once the file has been quiet this long.
	constexpr auto ReloadDelay = std::chrono::milliseconds(500);

	using FolderWatch = Utils::Win32::Closeable<HANDLE, FindCloseChangeNotification>;

	FolderWatch WatchFolder(const std::filesystem::path& folder, bool subtree) {
		return {FindFirstChangeNotificationW(folder.c_str(), subtree ? TRUE : FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE), INVALID_HANDLE_VALUE};
	}

	// Makes watches watch the folders of wanted that are there, and only those.
	void WatchFolders(std::map<std::filesystem::path, FolderWatch>& watches, const std::vector<std::filesystem::path>& wanted) {
		std::erase_if(watches, [&](const auto& w) { return std::ranges::find(wanted, w.first) == wanted.end(); });
		for (const auto& f : wanted) {
			if (!watches.contains(f) && is_directory(f))
				watches.emplace(f, WatchFolder(f, false));
		}
	}
}

void FontReplacement::PresetController::ThreadBody() {
	auto presetWatch = WatchFolder(m_presetFolder, true);
	std::map<std::filesystem::path, FolderWatch> glyphWatches;
	std::map<std::filesystem::path, FolderWatch> externalWatches;

	while (true) {
		{
			const auto lock = std::scoped_lock(m_mutex);
			if (m_stop)
				return;
		}

		RunPending();

		std::vector<std::filesystem::path> wantedGlyphFolders;
		std::vector<std::filesystem::path> wantedExternalFolders;
		{
			const auto lock = std::scoped_lock(m_mutex);
			wantedGlyphFolders = m_glyphFolders;
			for (const auto& path : m_externalPresets | std::views::keys) {
				if (std::ranges::find(wantedExternalFolders, path.parent_path()) == wantedExternalFolders.end())
					wantedExternalFolders.push_back(path.parent_path());
			}
		}
		WatchFolders(glyphWatches, wantedGlyphFolders);
		WatchFolders(externalWatches, wantedExternalFolders);

		std::vector<HANDLE> handles;
		std::vector<std::function<void()>> onSignal;
		if (presetWatch) {
			handles.push_back(*presetWatch);
			onSignal.emplace_back([&presetWatch] { FindNextChangeNotification(*presetWatch); });
		}
		for (const auto& [path, watch] : glyphWatches) {
			if (!watch)
				continue;
			handles.push_back(*watch);
			onSignal.emplace_back([&path, &watch] {
				FindNextChangeNotification(*watch);
				FontChanger::GlyphFiles::InvalidateFolder(path);
			});
		}
		std::vector<size_t> externalSignals;
		for (const auto& watch : externalWatches | std::views::values) {
			if (!watch)
				continue;
			externalSignals.push_back(handles.size());
			handles.push_back(*watch);
			onSignal.emplace_back([&watch] { FindNextChangeNotification(*watch); });
		}

		// Polls every so often too, as settings changes and the stop request don't signal the handles.
		DWORD timeout = 100;
		{
			const auto lock = std::scoped_lock(m_mutex);
			if (m_due) {
				const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*m_due - std::chrono::steady_clock::now()).count();
				timeout = static_cast<DWORD>(std::clamp<int64_t>(remaining, 0, 100));
			}
		}

		if (handles.empty()) {
			auto lock = std::unique_lock(m_mutex);
			m_wake.wait_for(lock, std::chrono::milliseconds(timeout), [this] { return m_stop || (m_due && std::chrono::steady_clock::now() >= *m_due); });
			continue;
		}

		const auto result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout);
		if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handles.size()) {
			const auto index = static_cast<size_t>(result - WAIT_OBJECT_0);
			onSignal[index]();

			// Other files in a preset's own folder change too; only its own change reloads.
			auto changed = true;
			if (std::ranges::find(externalSignals, index) != externalSignals.end()) {
				const auto lock = std::scoped_lock(m_mutex);
				changed = std::ranges::any_of(m_externalPresets, [](const auto& p) { return LastWriteTime(p.first) != p.second; });
			}
			if (changed)
				Schedule(true, ReloadDelay);
		}
	}
}
