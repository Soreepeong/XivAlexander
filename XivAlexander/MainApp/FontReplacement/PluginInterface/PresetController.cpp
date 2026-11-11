#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/PresetController.h"

#include <FontChanger.Presets/GlyphFiles.h>

#include "Config.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Edited files may be written in several steps, so they load a little later and a failure keeps the last loaded presets.
	constexpr uint64_t ReloadDelayMs = 500;

	constexpr uint64_t PollIntervalMs = 100;

	template<typename TWatch>
	void WatchFolders(std::map<std::filesystem::path, TWatch>& watchers, const std::set<std::filesystem::path>& folders) {
		std::erase_if(watchers, [&](const auto& w) { return !folders.contains(w.first); });
		for (const auto& folder : folders) {
			if (watchers.contains(folder))
				continue;
			constexpr DWORD Filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
			const auto handle = FindFirstChangeNotificationW(folder.c_str(), FALSE, Filter);

			// The folder may be gone since the loader looked for it.
			if (handle == INVALID_HANDLE_VALUE)
				FontReplacement::Host::Warning("Watching {} failed: error {}", xivres::util::unicode::convert<std::string>(folder.wstring()), GetLastError());
			watchers.emplace(folder, TWatch(handle, INVALID_HANDLE_VALUE));
		}
	}

	// Whether the folder changed since the last call.
	template<typename TWatch>
	bool TakeSignal(const TWatch& watch) {
		if (!watch || WaitForSingleObject(*watch, 0) != WAIT_OBJECT_0)
			return false;
		FindNextChangeNotification(*watch);
		return true;
	}
}

FontReplacement::PresetController::PresetController(std::shared_ptr<Config> config, FontLoader& loader)
	: m_config(std::move(config))
	, m_loader(loader)
	, m_presetFolder(Presets::Folder(*m_config)) {
	m_current.PresetFolder = m_presetFolder;
}

std::unique_ptr<FontReplacement::FontSet> FontReplacement::PresetController::Update() {
	if (std::exchange(m_settingsChanged, false)) {
		const auto& settings = m_config->Runtime.FontReplacement.Fonts;
		m_current.Families = settings.Families.Value();
		m_current.SystemFallback = settings.SystemFallback;
	}

	PollWatchers();
	const auto changed = m_current != m_requested;
	const auto ask = changed || (m_editedAt != 0 && GetTickCount64() - m_editedAt >= ReloadDelayMs);
	if (ask) {
		m_editedAt = 0;
		m_requested = m_current;
	}

	auto taken = m_loader.Take(ask ? std::optional(m_current) : std::nullopt, !changed);
	if (taken.Report) {
		SetStatus(std::move(taken.Report->Status));
		if (taken.Report->Watch)
			Watch(std::move(*taken.Report->Watch));
	}

	// The fonts requested last couldn't be made, and the earlier ones stay.
	if (taken.Error)
		SetStatus(std::format("Applying failed ({}); keeping the last loaded", *taken.Error));
	return std::move(taken.Fonts);
}

void FontReplacement::PresetController::SetStatus(std::string value) {
	if (value == m_status)
		return;
	m_status = std::move(value);
	Host::Information("{}", m_status);
}

void FontReplacement::PresetController::Watch(WatchList watch) {
	m_watchedPresets = std::move(watch.Presets);
	WatchFolders(m_presetWatchers, watch.PresetFolders);
	WatchFolders(m_folderWatchers, watch.GlyphImageFolders);
}

void FontReplacement::PresetController::PollWatchers() {
	const auto now = GetTickCount64();
	if (now - m_lastPoll < PollIntervalMs)
		return;
	m_lastPoll = now;

	auto edited = false;
	for (const auto& [folder, watch] : m_presetWatchers) {
		if (!TakeSignal(watch))
			continue;
		for (auto& [path, time] : m_watchedPresets) {
			if (path.parent_path() != folder)
				continue;
			if (const auto written = WatchList::LastWriteOf(path); written != time) {
				time = written;
				edited = true;
			}
		}
	}

	for (const auto& [folder, watch] : m_folderWatchers) {
		if (!TakeSignal(watch))
			continue;
		FontChanger::GlyphFiles::InvalidateFolder(folder);
		edited = true;
	}

	if (edited)
		m_editedAt = now;
}
