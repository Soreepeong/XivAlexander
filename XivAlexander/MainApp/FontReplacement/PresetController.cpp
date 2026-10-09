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

	using FolderWatch = Utils::Win32::Closeable<HANDLE, FindCloseChangeNotification>;

	FolderWatch WatchFolder(const std::filesystem::path& folder, bool subtree) {
		return {FindFirstChangeNotificationW(folder.c_str(), subtree ? TRUE : FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE), INVALID_HANDLE_VALUE};
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
	auto presetWatch = WatchFolder(m_presetFolder, true);
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
			try {
				Load(keepOnFailure);
			} catch (const std::exception& e) {
				Host::Error("Loading the presets failed: {}", e.what());
				const auto lock = std::scoped_lock(m_mutex);
				m_status = std::format("Loading failed ({})", e.what());
			}
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
			onSignal[result - WAIT_OBJECT_0]();
			Schedule(true, ReloadDelay);
		}
	}
}

void FontReplacement::PresetController::Load(bool keepOnFailure) {
	const auto& settings = m_config->Runtime.FontReplacement.Faces;
	const auto families = settings.Families.Value();
	const bool systemFallback = settings.SystemFallback;

	std::vector<std::string> failures;
	PresetReader read(m_presetFolder, failures);

	Presets::Faces preset;
	auto fontFamilies = 0;
	for (const auto& [family, familySettings] : families) {
		auto usesFont = false;
		Presets::Combine(preset, MakeFamilyFaces(family, familySettings, read, failures, &usesFont));
		fontFamilies += usesFont ? 1 : 0;
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
	const auto presetCount = read.ReadCount();
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

FontReplacement::PresetController::PresetReader::PresetReader(std::filesystem::path folder, std::vector<std::string>& failures)
	: m_folder(std::move(folder))
	, m_failures(failures) {
}

const std::optional<FontReplacement::Presets::Faces>& FontReplacement::PresetController::PresetReader::operator()(const std::string& preset) {
	if (const auto it = m_loaded.find(preset); it != m_loaded.end())
		return it->second;
	try {
		return m_loaded.emplace(preset, Presets::Load(Presets::Resolve(m_folder, preset))).first->second;
	} catch (const std::exception& e) {
		Host::Error("Loading the preset {} failed: {}", preset, e.what());
		m_failures.push_back(std::format("{}: {}", preset, e.what()));
		return m_loaded.emplace(preset, std::nullopt).first->second;
	}
}

size_t FontReplacement::PresetController::PresetReader::ReadCount() const {
	return static_cast<size_t>(std::ranges::count_if(m_loaded | std::views::values, [](const auto& p) { return p.has_value(); }));
}

FontReplacement::Presets::Faces FontReplacement::PresetController::MakeFamilyFaces(const std::string& family, const FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool* usesFont) {
	Presets::Faces combined;
	if (!settings.Enabled)
		return combined;
	for (const auto& source : settings.Sources) {
		if (!source.Enabled)
			continue;
		if (source.IsPreset()) {
			if (const auto& faces = read(source.Preset))
				Presets::Combine(combined, Presets::FacesOfFamily(*faces, family));
		} else if (const auto generated = MakeFaces(family, source.Font, settings.MonospacedDigits, failures)) {
			Presets::Combine(combined, *generated);
			if (usesFont)
				*usesFont = true;
		}
	}
	return combined;
}

std::optional<FontReplacement::Presets::Faces> FontReplacement::PresetController::MakeFaces(const std::string& family, const FontReplacementFamilyFont& font, bool monospacedDigits, std::vector<std::string>& failures) {
	using FontChanger::FaceFromFont::FontInfo;

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
			auto elements = FontChanger::FaceFromFont::MakeElements(GameFontNames::FamilyOf(name), size, lookup, *info, open, monospacedDigits);
			if (elements.empty())
				continue;

			// The game's glyphs go first, as in FaceFromFont.MakeFace: MakeElements' Replace-mode elements draw nothing without an earlier element.
			// It also gives the face the game font's sizes and line metrics, as in a preset.
			auto face = std::make_shared<FontChanger::Structs::Face>();
			face->Name = name;
			auto& game = *face->Elements.emplace_back(std::make_unique<FontChanger::Structs::FaceElement>());
			game.Renderer = FontChanger::Structs::RendererEnum::PrerenderedGameInstallation;
			game.Lookup.Name = GameFontNames::FamilyOf(name);
			game.Size = size;
			game.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
			std::ranges::move(elements, std::back_inserter(face->Elements));
			faces.emplace(name, std::move(face));
		}
		return faces.empty() ? std::nullopt : std::optional(std::move(faces));
	} catch (const std::exception& e) {
		Host::Error("Making the faces of {} with {} failed: {}", family, font.Name, e.what());
		failures.push_back(std::format("{}: {}", family, e.what()));
		return std::nullopt;
	}
}
