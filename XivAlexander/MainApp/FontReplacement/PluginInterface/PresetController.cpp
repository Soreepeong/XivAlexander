#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/PresetController.h"

#include <FontChanger.Presets/FontsFromSystemFont.h>
#include <FontChanger.Presets/GlyphFiles.h>

#include "Config.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/GameFontDataFaceSource.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "MainApp/FontReplacement/Host.h"
#include "Utils/Win32/Closeable.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Editors write a file in several steps; a change is acted on once the file has been quiet this long.
	constexpr auto ReloadDelay = std::chrono::milliseconds(500);

	using FolderWatch = Utils::Win32::Closeable<HANDLE, FindCloseChangeNotification>;

	FolderWatch WatchFolder(const std::filesystem::path& folder, bool subtree) {
		return {FindFirstChangeNotificationW(folder.c_str(), subtree ? TRUE : FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE), INVALID_HANDLE_VALUE};
	}

	void WatchFolders(std::map<std::filesystem::path, FolderWatch>& watches, const std::vector<std::filesystem::path>& wanted) {
		std::erase_if(watches, [&](const auto& w) { return std::ranges::find(wanted, w.first) == wanted.end(); });
		for (const auto& f : wanted) {
			if (!watches.contains(f) && is_directory(f))
				watches.emplace(f, WatchFolder(f, false));
		}
	}

	std::optional<std::filesystem::file_time_type> LastWriteTime(const std::filesystem::path& path) {
		std::error_code ec;
		const auto time = last_write_time(path, ec);
		return ec ? std::nullopt : std::optional(time);
	}

	FontChanger::Structs::LookupStruct ToLookup(const XivAlexander::FontReplacementFamilyFont& font) {
		FontChanger::Structs::LookupStruct lookup;
		lookup.FamilyName = font.FamilyName;
		lookup.Weight = static_cast<DWRITE_FONT_WEIGHT>(font.Weight);
		lookup.Stretch = static_cast<DWRITE_FONT_STRETCH>(font.Stretch);
		lookup.Style = static_cast<DWRITE_FONT_STYLE>(font.Style);
		return lookup;
	}
}

FontReplacement::PresetController::PresetController(std::shared_ptr<Config> config, Apply apply)
	: m_config(std::move(config))
	, m_apply(std::move(apply))
	, m_presetFolder(Presets::Folder(*m_config))
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

void FontReplacement::PresetController::RunPending() {
	bool keepOnFailure;
	{
		const auto lock = std::scoped_lock(m_mutex);
		if (!m_pendingLoad || !m_due || std::chrono::steady_clock::now() < *m_due)
			return;
		keepOnFailure = !m_pendingSettingsLoad;
		m_pendingLoad = m_pendingSettingsLoad = false;
		m_due.reset();
	}

	try {
		Load(keepOnFailure);
	} catch (const std::exception& e) {
		Host::Error("Loading the presets failed: {}", e.what());
	}
}

void FontReplacement::PresetController::Load(bool keepOnFailure) {
	// Each is read once, as a change of the settings replaces them from another thread.
	const auto& settings = m_config->Runtime.FontReplacement.Fonts;
	const auto families = settings.Families.Value();
	const bool systemFallback = settings.SystemFallback;

	std::vector<std::string> failures;
	PresetReader read(m_presetFolder, failures);

	Presets::Fonts preset;
	auto fontFamilies = 0;
	for (const auto& [family, familySettings] : families) {
		auto usesFont = false;
		Presets::Combine(preset, MakeFamilyFonts(family, familySettings, read, failures, usesFont));
		fontFamilies += usesFont ? 1 : 0;
	}

	std::string failureText;
	for (const auto& failure : failures)
		failureText += (failureText.empty() ? "" : "; ") + failure;

	if (keepOnFailure && !failures.empty()) {
		Host::Warning("Changes not applied ({}); keeping the last loaded", failureText);
		return;
	}

	std::set<std::string, LessIgnoringCase> presetFamilies;
	for (const auto& name : preset | std::views::keys)
		presetFamilies.insert(Presets::FamilyOf(name));
	const auto presetCount = read.ReadCount();
	auto status = preset.empty()
		? std::string("The game's fonts")
		: std::format("{} fonts of {} families", preset.size(), presetFamilies.size())
		+ (presetCount ? std::format(" from {} presets", presetCount) : std::string())
		+ (fontFamilies ? std::format("; system fonts for {}", fontFamilies) : std::string());
	if (!failures.empty())
		status += std::format("; loading failed ({})", failureText);
	Host::Information("{}", status);

	std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> externalPresets;
	for (const auto& path : read.Paths()) {
		if (const auto rel = path.lexically_normal().lexically_relative(m_presetFolder.lexically_normal()); !rel.empty() && *rel.begin() != L"..")
			continue;
		externalPresets.emplace(path, LastWriteTime(path));
	}

	{
		const auto lock = std::scoped_lock(m_mutex);
		m_glyphFolders = Presets::GlyphImageFolders(preset);
		m_externalPresets = std::move(externalPresets);
	}

	m_apply(std::move(preset), systemFallback);
}

FontReplacement::PresetController::PresetReader::PresetReader(std::filesystem::path folder, std::vector<std::string>& failures)
	: m_folder(std::move(folder))
	, m_failures(failures) {
}

const std::optional<FontReplacement::Presets::Fonts>& FontReplacement::PresetController::PresetReader::operator()(const std::string& preset) {
	if (const auto it = m_loaded.find(preset); it != m_loaded.end())
		return it->second;
	try {
		const auto path = Presets::Resolve(m_folder, preset);
		m_paths.push_back(path);
		return m_loaded.emplace(preset, Presets::Load(path)).first->second;
	} catch (const std::exception& e) {
		Host::Error("Loading the preset {} failed: {}", preset, e.what());
		m_failures.push_back(std::format("{}: {}", preset, e.what()));
		return m_loaded.emplace(preset, std::nullopt).first->second;
	}
}

size_t FontReplacement::PresetController::PresetReader::ReadCount() const {
	return static_cast<size_t>(std::ranges::count_if(m_loaded | std::views::values, [](const auto& p) { return p.has_value(); }));
}

FontReplacement::Presets::Fonts FontReplacement::PresetController::MakeFamilyFonts(const std::string& family, const FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool& usesFont) {
	Presets::Fonts combined;
	if (!settings.Enabled)
		return combined;

	// Each source's fonts go under those of the sources after it (the last on top), so they are made from the last and overlaid from the first.
	std::vector<std::optional<Presets::Fonts>> layers(settings.Sources.size());
	std::vector<const Presets::Fonts*> above;
	for (auto i = settings.Sources.size(); i-- > 0;) {
		const auto& source = settings.Sources[i];
		if (!source.Enabled)
			continue;
		if (source.IsPreset()) {
			if (const auto& fonts = read(source.Preset))
				layers[i] = Presets::FontsOfFamily(*fonts, family);
		} else if (auto generated = MakeFonts(family, source.Font, source.MonospacedDigits, source.Adjustments, above, failures)) {
			layers[i] = std::move(*generated);
			usesFont = true;
		}
		if (layers[i])
			above.push_back(&*layers[i]);
	}
	for (const auto& layer : layers) {
		if (layer)
			Presets::Overlay(combined, *layer);
	}
	return combined;
}

std::optional<FontReplacement::Presets::Fonts> FontReplacement::PresetController::MakeFonts(
	const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, const FontReplacementAdjustments& adjustments, const std::vector<const Presets::Fonts*>& above,
	std::vector<std::string>& failures) {
	using FontChanger::FontsFromSystemFont::FontInfo;

	std::map<std::tuple<std::string, DWRITE_FONT_WEIGHT, DWRITE_FONT_STRETCH, DWRITE_FONT_STYLE>, std::optional<FontInfo>> opened;
	const FontChanger::FontsFromSystemFont::FontOpener open = [&opened](const FontChanger::Structs::LookupStruct& lookup) -> const FontInfo* {
		const auto key = std::make_tuple(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style);
		auto it = opened.find(key);
		if (it == opened.end())
			it = opened.emplace(key, FontInfo::Open(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style)).first;
		return it->second ? &*it->second : nullptr;
	};

	try {
		const auto lookup = ToLookup(font);
		if (!open(lookup)) {
			failures.push_back(std::format("{}: the font {} isn't installed", family, font.FamilyName));
			return std::nullopt;
		}

		const FontChanger::FontsFromSystemFont::FontAdjustments adjusted{
			.Scripts = static_cast<FontChanger::FontsFromSystemFont::FontScripts>(adjustments.Scripts),
			.NonCjkSize = adjustments.NonCjkSize,
			.CjkSize = adjustments.CjkSize,
			.Raise = adjustments.Raise,
			.NonCjkSpacing = adjustments.NonCjkSpacing,
			.CjkSpacing = adjustments.CjkSpacing,
			.StrokeWeight = static_cast<FontChanger::FontsFromSystemFont::StrokeWeight>(std::clamp(adjustments.StrokeWeight, 0, 3)),
		};

		Presets::Fonts res;
		for (const auto& [name, size] : FontNames::FontNamesOf(family)) {
			const auto gameFont = GameFontDataFaceSource::OpenMetrics(name);

			// MakeFont fits this font to what the same-named fonts above draw, so it is given their probes.
			std::vector<std::unique_ptr<FontChanger::FontsFromSystemFont::FontProbe>> probes;
			std::vector<const FontChanger::FontsFromSystemFont::FontProbe*> fontsAbove;
			for (const auto fonts : above) {
				if (const auto it = fonts->find(name); it != fonts->end())
					fontsAbove.push_back(probes.emplace_back(FontChanger::FontsFromSystemFont::ProbeFont(*it->second, open)).get());
			}
			if (auto made = FontChanger::FontsFromSystemFont::MakeFont(name, size, lookup, open, monospacedDigits, gameFont.get(), fontsAbove, adjusted))
				res.emplace(name, std::move(made));
		}
		return res.empty() ? std::nullopt : std::optional(std::move(res));
	} catch (const std::exception& e) {
		Host::Error("Making the fonts of {} with {} failed: {}", family, font.FamilyName, e.what());
		failures.push_back(std::format("{}: {}", family, e.what()));
		return std::nullopt;
	}
}
