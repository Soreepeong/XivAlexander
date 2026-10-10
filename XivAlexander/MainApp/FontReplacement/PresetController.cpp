#include "pch.h"
#include "MainApp/FontReplacement/PresetController.h"

#include <FontChanger.Presets/FaceFromFont.h>
#include <FontChanger.Presets/GlyphFiles.h>

#include "Config.h"
#include "MainApp/FontReplacement/GameFontNames.h"
#include "MainApp/FontReplacement/Host.h"
#include "Utils/Win32/Closeable.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Editors write a file in several steps; a change is acted on once the file has been quiet this long.
	constexpr auto ReloadDelay = std::chrono::milliseconds(500);

	// A change notification of a folder, closed when destroyed; empty if the folder can't be watched.
	using FolderWatch = Utils::Win32::Closeable<HANDLE, FindCloseChangeNotification>;

	// Watches a folder for files coming, going and being written.
	FolderWatch WatchFolder(const std::filesystem::path& folder, bool subtree) {
		return {FindFirstChangeNotificationW(folder.c_str(), subtree ? TRUE : FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE), INVALID_HANDLE_VALUE};
	}
}

FontReplacement::PresetController::PresetController(std::shared_ptr<Config> config, Apply apply)
	: m_config(std::move(config))
	, m_apply(std::move(apply))
	, m_thread([this] { ThreadBody(); }) {
	Reload();
}

FontReplacement::PresetController::~PresetController() {
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

std::string FontReplacement::PresetController::Status() {
	const auto lock = std::scoped_lock(m_mutex);
	return m_status;
}

void FontReplacement::PresetController::Reload() {
	Schedule(false, std::chrono::milliseconds(0));
}

void FontReplacement::PresetController::Schedule(bool edited, std::chrono::milliseconds delay) {
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_pendingLoad = true;
		m_pendingSettingsLoad |= !edited;
		m_due = std::chrono::steady_clock::now() + delay;
	}
	m_wake.notify_all();
}

void FontReplacement::PresetController::ThreadBody() {
	FolderWatch presetWatch;
	std::filesystem::path watchedFolder;
	std::map<std::filesystem::path, FolderWatch> glyphWatches;

	while (true) {
		bool load = false, keepOnFailure = false;
		{
			auto lock = std::unique_lock(m_mutex);
			if (m_stop)
				return;
			if (m_pendingLoad && m_due && std::chrono::steady_clock::now() >= *m_due) {
				load = true;
				keepOnFailure = !m_pendingSettingsLoad;
				m_pendingLoad = m_pendingSettingsLoad = false;
				m_due.reset();
			}
		}

		if (load) {
			// One at a time, in order: each applies the settings as they are when it starts.
			try {
				Load(keepOnFailure);
			} catch (const std::exception& e) {
				Host::Error("Loading the presets failed: {}", e.what());
				const auto lock = std::scoped_lock(m_mutex);
				m_status = std::format("Loading failed ({})", e.what());
			}
		}

		// The preset folder, and the folders of the presets' glyph images, and no others.
		const auto folder = m_config->Runtime.FontReplacement.Faces.PresetFolder.Value();
		if (folder != watchedFolder) {
			watchedFolder = folder;
			presetWatch.Clear();
			if (!folder.empty() && is_directory(folder))
				presetWatch = WatchFolder(folder, true);
		}

		std::vector<std::filesystem::path> wantedGlyphFolders;
		{
			const auto lock = std::scoped_lock(m_mutex);
			wantedGlyphFolders = m_glyphFolders;
		}
		std::erase_if(glyphWatches, [&](const auto& w) { return std::ranges::find(wantedGlyphFolders, w.first) == wantedGlyphFolders.end(); });
		for (const auto& f : wantedGlyphFolders) {
			if (!glyphWatches.contains(f) && is_directory(f))
				glyphWatches.emplace(f, WatchFolder(f, false));
		}

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

		// Waits for a change of files, polling for the settings' and the stop request every so often.
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
			onSignal[result - WAIT_OBJECT_0]();
			Schedule(true, ReloadDelay);
		}
	}
}

void FontReplacement::PresetController::Load(bool keepOnFailure) {
	const auto& settings = m_config->Runtime.FontReplacement.Faces;
	const auto folder = settings.PresetFolder.Value();
	const auto families = settings.FamilyPresets.Value();
	const auto fonts = settings.FamilyFonts.Value();
	const bool monospacedDigits = settings.MonospacedDigits;
	const bool systemFallback = settings.SystemFallback;

	// Each preset is read once, however many families use it.
	std::map<std::string, std::optional<Presets::Faces>, LessIgnoringCase> loaded;
	std::vector<std::string> failures;
	const auto read = [&](const std::string& relative) -> const std::optional<Presets::Faces>& {
		if (const auto it = loaded.find(relative); it != loaded.end())
			return it->second;
		const auto path = folder / std::filesystem::path(xivres::util::unicode::convert<std::wstring>(relative));
		try {
			return loaded.emplace(relative, Presets::Load(path)).first->second;
		} catch (const std::exception& e) {
			Host::Error("Loading the preset {} failed: {}", relative, e.what());
			failures.push_back(std::format("{}: {}", relative, e.what()));
			return loaded.emplace(relative, std::nullopt).first->second;
		}
	};

	Presets::Faces preset;
	for (const auto& [family, selected] : families) {
		Presets::Faces combined;
		for (const auto& relative : selected) {
			if (const auto& faces = read(relative))
				Presets::Combine(combined, *faces);
		}
		Presets::Combine(preset, Presets::OnlyFamily(combined, family));
	}

	// The families' system fonts, over their presets.
	auto fontFamilies = 0;
	for (const auto& [family, font] : fonts) {
		if (const auto generated = MakeFaces(family, font, monospacedDigits, failures)) {
			Presets::Combine(preset, *generated);
			fontFamilies++;
		}
	}

	std::string failureText;
	for (const auto& failure : failures)
		failureText += (failureText.empty() ? "" : "; ") + failure;

	if (keepOnFailure && !failures.empty()) {
		const auto lock = std::scoped_lock(m_mutex);
		m_status = std::format("Changes not applied ({}); keeping the last loaded", failureText);
		Host::Warning("{}", m_status);
		return;
	}

	std::set<std::string, LessIgnoringCase> presetFamilies;
	for (const auto& name : preset | std::views::keys)
		presetFamilies.insert(GameFontNames::FamilyOf(name));
	const auto presetCount = std::ranges::count_if(loaded | std::views::values, [](const auto& p) { return p.has_value(); });
	auto status = preset.empty()
		? std::string("The game's fonts")
		: std::format("{} faces of {} families", preset.size(), presetFamilies.size())
		+ (presetCount ? std::format(" from {} presets", presetCount) : std::string())
		+ (fontFamilies ? std::format("; system fonts for {}", fontFamilies) : std::string());
	if (!failures.empty())
		status += std::format("; loading failed ({})", failureText);
	Host::Information("{}", status);

	{
		const auto lock = std::scoped_lock(m_mutex);
		m_status = std::move(status);
		m_glyphFolders = Presets::GlyphImageFolders(preset);
	}

	m_apply(std::move(preset), systemFallback);
}

std::optional<FontReplacement::Presets::Faces> FontReplacement::PresetController::MakeFaces(const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, std::vector<std::string>& failures) {
	using FontChanger::FaceFromFont::FontInfo;

	// Each font is opened once for all the faces.
	std::map<std::tuple<std::string, DWRITE_FONT_WEIGHT, DWRITE_FONT_STRETCH, DWRITE_FONT_STYLE>, std::optional<FontInfo>> opened;
	const FontChanger::FaceFromFont::FontOpener open = [&opened](const FontChanger::Structs::LookupStruct& lookup) -> const FontInfo* {
		const auto key = std::make_tuple(lookup.Name, lookup.Weight, lookup.Stretch, lookup.Style);
		auto it = opened.find(key);
		if (it == opened.end())
			it = opened.emplace(key, FontInfo::Open(lookup.Name, lookup.Weight, lookup.Stretch, lookup.Style)).first;
		return it->second ? &*it->second : nullptr;
	};

	try {
		FontChanger::Structs::LookupStruct lookup;
		lookup.Name = font.Name;
		lookup.Weight = static_cast<DWRITE_FONT_WEIGHT>(font.Weight);
		lookup.Stretch = static_cast<DWRITE_FONT_STRETCH>(font.Stretch);
		lookup.Style = static_cast<DWRITE_FONT_STYLE>(font.Style);
		const auto info = open(lookup);
		if (!info) {
			failures.push_back(std::format("{}: the font {} isn't installed", family, font.Name));
			return std::nullopt;
		}

		Presets::Faces faces;
		for (const auto& [name, size] : GameFontNames::FacesOf(family)) {
			auto face = std::make_shared<FontChanger::Structs::Face>();
			face->Name = name;
			face->Elements = FontChanger::FaceFromFont::MakeElements(GameFontNames::FamilyOf(name), size, lookup, *info, open, monospacedDigits);
			if (!face->Elements.empty())
				faces.emplace(name, std::move(face));
		}
		return faces.empty() ? std::nullopt : std::optional(std::move(faces));
	} catch (const std::exception& e) {
		Host::Error("Making the faces of {} with {} failed: {}", family, font.Name, e.what());
		failures.push_back(std::format("{}: {}", family, e.what()));
		return std::nullopt;
	}
}
