#include "pch.h"
#include "MainApp/FontReplacement/PluginInterface/PresetController.h"

#include <FontChanger.Presets/FontsFromSystemFont.h>

#include "Config.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/GameFontDataFaceSource.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::PresetController::Schedule(bool edited, std::chrono::milliseconds delay) {
	{
		const auto lock = std::scoped_lock(m_mutex);
		m_pendingLoad = true;
		m_pendingSettingsLoad |= !edited;
		m_due = std::chrono::steady_clock::now() + delay;
	}
	m_wake.notify_all();
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
		const auto lock = std::scoped_lock(m_mutex);
		m_status = std::format("Loading failed ({})", e.what());
	}
}

std::optional<std::filesystem::file_time_type> FontReplacement::PresetController::LastWriteTime(const std::filesystem::path& path) {
	std::error_code ec;
	const auto time = last_write_time(path, ec);
	return ec ? std::nullopt : std::optional(time);
}

void FontReplacement::PresetController::Load(bool keepOnFailure) {
	// Each is read once: a change of the settings replaces them, from another thread.
	const auto& settings = m_config->Runtime.FontReplacement.Fonts;
	const auto families = settings.Families.Value();
	const bool systemFallback = settings.SystemFallback;

	std::vector<std::string> failures;
	PresetReader read(m_presetFolder, failures);

	Presets::Fonts preset;
	auto fontFamilies = 0;
	for (const auto& [family, familySettings] : families) {
		auto usesFont = false;
		Presets::Combine(preset, MakeFamilyFonts(family, familySettings, read, failures, &usesFont));
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
		m_status = std::move(status);
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

FontReplacement::Presets::Fonts FontReplacement::PresetController::MakeFamilyFonts(const std::string& family, const FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool* usesFont) {
	Presets::Fonts combined;
	if (!settings.Enabled)
		return combined;
	// Later sources are drawn over earlier ones: the fonts of each are made from the last, under the fonts of those after it
	// (the last on top), then overlaid from the first.
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
			if (usesFont)
				*usesFont = true;
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

FontChanger::Structs::LookupStruct FontReplacement::PresetController::ToLookup(const FontReplacementFamilyFont& font) {
	FontChanger::Structs::LookupStruct lookup;
	lookup.FamilyName = font.FamilyName;
	lookup.Weight = static_cast<DWRITE_FONT_WEIGHT>(font.Weight);
	lookup.Stretch = static_cast<DWRITE_FONT_STRETCH>(font.Stretch);
	lookup.Style = static_cast<DWRITE_FONT_STYLE>(font.Style);
	return lookup;
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

			// The fonts of the same name above, as drawn.
			std::vector<std::unique_ptr<FontChanger::FontsFromSystemFont::FontProbe>> probes;
			std::vector<const FontChanger::FontsFromSystemFont::FontProbe*> fontsAbove;
			for (const auto fonts : above) {
				if (const auto it = fonts->find(name); it != fonts->end())
					fontsAbove.push_back(probes.emplace_back(FontChanger::FontsFromSystemFont::ProbeFont(*it->second, open)).get());
			}
			auto elements = FontChanger::FontsFromSystemFont::MakeElements(name, size, lookup, open, monospacedDigits, gameFont.get(), fontsAbove, adjusted);
			if (elements.empty())
				continue;

			// The game's glyphs go first, as in FontsFromSystemFont.MakeFont: MakeElements' Replace-mode elements draw nothing without an earlier element.
			// It also gives the font the game font's sizes and line metrics, as in a preset.
			auto made = std::make_shared<FontChanger::Structs::Font>();
			made->Name = name;
			auto& game = *made->Elements.emplace_back(std::make_unique<FontChanger::Structs::FontElement>());
			game.Source = FontChanger::Structs::ElementSourceEnum::Game;
			game.Lookup.FamilyName = Presets::FamilyOf(name);
			game.Size = size;
			game.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
			std::ranges::move(elements, std::back_inserter(made->Elements));
			res.emplace(name, std::move(made));
		}
		return res.empty() ? std::nullopt : std::optional(std::move(res));
	} catch (const std::exception& e) {
		Host::Error("Making the fonts of {} with {} failed: {}", family, font.FamilyName, e.what());
		failures.push_back(std::format("{}: {}", family, e.what()));
		return std::nullopt;
	}
}
