#include "pch.h"
#include "MainApp/FontReplacement/Fonts/PresetCompiler.h"

#include <FontChanger.Presets/FontsFromSystemFont.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/GameFontDataFaceSource.h"
#include "MainApp/FontReplacement/Game/GameFontTable.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
namespace Presets = FontReplacement::Presets;

namespace {
	// Unreadable presets read as nullopt, with why in failures.
	class PresetReader {
		const std::filesystem::path m_folder;
		std::vector<std::string>& m_failures;
		std::map<std::string, std::optional<Presets::Fonts>, FontReplacement::LessIgnoringCase> m_loaded;
		std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>> m_paths;

	public:
		PresetReader(std::filesystem::path folder, std::vector<std::string>& failures)
			: m_folder(std::move(folder))
			, m_failures(failures) {}

		const std::optional<Presets::Fonts>& operator()(const std::string& preset) {
			if (const auto it = m_loaded.find(preset); it != m_loaded.end())
				return it->second;
			try {
				const auto path = Presets::Resolve(m_folder, preset);

				// Taken before reading, so that an edit made meanwhile is seen as one.
				m_paths.emplace(path, FontReplacement::WatchList::LastWriteOf(path));
				return m_loaded.emplace(preset, Presets::Load(path)).first->second;
			} catch (const std::exception& e) {
				FontReplacement::Host::Error("Loading the preset {} failed: {}", preset, e.what());
				m_failures.push_back(std::format("{}: {}", preset, e.what()));
				return m_loaded.emplace(preset, std::nullopt).first->second;
			}
		}

		[[nodiscard]] size_t ReadCount() const {
			return static_cast<size_t>(std::ranges::count_if(m_loaded | std::views::values, [](const auto& p) { return p.has_value(); }));
		}

		// Of every preset asked for, read or not, by when each was last written.
		[[nodiscard]] const std::map<std::filesystem::path, std::optional<std::filesystem::file_time_type>>& Paths() const { return m_paths; }
	};

	FontChanger::Structs::LookupStruct ToLookup(const XivAlexander::FontReplacementFamilyFont& font) {
		FontChanger::Structs::LookupStruct lookup;
		lookup.FamilyName = font.FamilyName;
		lookup.Weight = static_cast<DWRITE_FONT_WEIGHT>(font.Weight);
		lookup.Stretch = static_cast<DWRITE_FONT_STRETCH>(font.Stretch);
		lookup.Style = static_cast<DWRITE_FONT_STYLE>(font.Style);
		return lookup;
	}

	// The fonts are made to go under those of the sources above, which are listed top first.
	std::optional<Presets::Fonts> MakeFonts(
		const std::string& family, const XivAlexander::FontReplacementFamilyFont& font, bool monospacedDigits, const XivAlexander::FontReplacementAdjustments& adjustments,
		const std::vector<const Presets::Fonts*>& above, std::vector<std::string>& failures) {
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
			for (const auto& [name, size] : FontReplacement::GameFontTable::FontNamesOf(family)) {
				const auto gameFont = FontReplacement::GameFontDataFaceSource::OpenMetrics(name);

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
			FontReplacement::Host::Error("Making the fonts of {} with {} failed: {}", family, font.FamilyName, e.what());
			failures.push_back(std::format("{}: {}", family, e.what()));
			return std::nullopt;
		}
	}

	// Empty if the family is off, so that the game's font stays.
	Presets::Fonts MakeFamilyFonts(const std::string& family, const XivAlexander::FontReplacementFamily& settings, PresetReader& read, std::vector<std::string>& failures, bool& usesFont) {
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
}

std::pair<std::optional<FontReplacement::FontSettings>, FontReplacement::LoadReport> FontReplacement::PresetCompiler::Compile(const FontRequest& request, bool keepOnFailure) {
	std::vector<std::string> failures;
	PresetReader read(request.PresetFolder, failures);

	Presets::Fonts preset;
	auto fontFamilies = 0;
	for (const auto& [family, familySettings] : request.Families) {
		auto usesFont = false;
		Presets::Combine(preset, MakeFamilyFonts(family, familySettings, read, failures, usesFont));
		fontFamilies += usesFont ? 1 : 0;
	}

	std::string failureText;
	for (const auto& failure : failures)
		failureText += (failureText.empty() ? "" : "; ") + failure;

	if (keepOnFailure && !failures.empty())
		return {std::nullopt, {std::format("Changes not applied ({}); keeping the last loaded", failureText), std::nullopt}};

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

	auto watch = WatchList::Of(read.Paths(), Presets::GlyphImageFolders(preset));
	return {FontSettings{std::move(preset), request.SystemFallback}, LoadReport{std::move(status), std::move(watch)}};
}
