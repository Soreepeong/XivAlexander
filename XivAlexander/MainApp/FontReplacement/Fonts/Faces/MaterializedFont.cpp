#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"

#include <FontChanger.Presets/FontsFromSystemFont.h>

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Presets.h"
#include "MainApp/FontReplacement/Game/FontNames.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/EmptyFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/GameFontDataFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/ImageFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/MergingFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Text/SystemFallbackFonts.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::FontElement;
using FontChanger::Structs::ElementSourceEnum;
using namespace FontChanger::FixedSizeFont;

FontReplacement::MaterializedFont::MaterializedFont(FontServices& services, std::shared_ptr<const FontChanger::Structs::Font> def, bool systemFallback, SystemFallbackFonts& fallbackFonts)
	: m_services(services)
	, m_def(std::move(def))
	, m_fallbackFonts(fallbackFonts)
	, m_systemFallback(systemFallback)
	, m_referenceSize(!m_def->Elements.empty() && m_def->Elements[0]->Size > 0 ? m_def->Elements[0]->Size : 1.f) {
	FontElement gameDef;
	gameDef.Size = m_referenceSize;
	gameDef.Source = ElementSourceEnum::Game;
	m_gameElement = std::make_unique<MaterializedFontElement>(std::move(gameDef), nullptr, m_referenceSize);

	for (const auto& e : m_def->Elements)
		m_elements.push_back(std::make_unique<MaterializedFontElement>(*e, CreateSource(*e, services, m_def->Name), m_referenceSize));

	// A font that isn't installed gets an EmptyFaceSource.
	for (const auto& e : m_elements) {
		const auto renderer = e->Def().Source;
		if ((renderer == ElementSourceEnum::DirectWrite || renderer == ElementSourceEnum::FreeType) && !dynamic_cast<EmptyFaceSource*>(e->Source())) {
			m_primary = e.get();
			break;
		}
	}
	m_measureMode = m_primary ? m_primary->Def().RendererSpecific.DirectWrite.MeasureMode : DWRITE_MEASURING_MODE_NATURAL;
	for (const auto& e : m_elements) {
		if (const auto source = e->Source())
			source->SetMeasureMode(m_measureMode);
	}
}

FontReplacement::MaterializedFont::~MaterializedFont() = default;

std::unique_ptr<FontReplacement::MaterializedFont> FontReplacement::MaterializedFont::CreateGame(FontServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts) {
	auto font = std::make_shared<FontChanger::Structs::Font>();
	font->Name = "(game)";
	auto& e = *font->Elements.emplace_back(std::make_unique<FontElement>());
	e.Size = 1;
	e.Source = ElementSourceEnum::Game;
	e.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
	return std::make_unique<MaterializedFont>(services, std::move(font), systemFallback, fallbackFonts);
}

FontReplacement::MaterializedFontElement* FontReplacement::MaterializedFont::TryGetElement(std::span<const char32_t> grapheme, GameFont* game) {
	if (grapheme.empty())
		return nullptr;

	if (grapheme.size() == 1) {
		const auto key = std::make_pair(game, grapheme[0]);
		if (const auto it = m_elementCache.find(key); it != m_elementCache.end())
			return it->second;
		const auto element = FindElement(grapheme, game);
		m_elementCache.emplace(key, element);
		return element;
	}

	if (const auto assigned = FindAssigned(grapheme, game))
		return assigned;
	return TryGetElement(grapheme.first(1), game);
}

FontReplacement::LineMetrics FontReplacement::MaterializedFont::GetLineMetrics(float px, GameFont* game) {
	auto metrics = m_elements.empty() ? LineMetrics{} : GetElementMetrics(*m_elements[0], px, game);
	if (metrics.LineHeight <= 0) {
		// Segoe UI's proportions.
		metrics.Ascent = static_cast<int>(std::ceil(px * 1.08f));
		metrics.LineHeight = static_cast<int>(std::ceil(px * 1.33f));
	}
	return metrics;
}

FontReplacement::FontAlignment FontReplacement::MaterializedFont::GetAlignment(float px, GameFont* game) {
	const auto [ascent, lineHeight] = GetLineMetrics(px, game);
	const auto first = m_elements.empty() ? nullptr : m_elements[0].get();
	return {
		.Ascent = ascent,
		.LineHeight = lineHeight,
		.RomanBaselineY = first ? first->GetRomanBaselineY(px, ascent) : static_cast<float>(ascent),
		.IdeographicCenterY = first ? first->GetIdeographicCenterY(px, ascent, lineHeight) : static_cast<float>(lineHeight) / 2,
		.Mode = m_def->VerticalAlignment,
	};
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::MaterializedFont::TryRasterize(char32_t codepoint, float px, GameFont* game) {
	const auto element = TryGetElement(std::span(&codepoint, 1), game);
	const auto source = element ? element->Source() : nullptr;
	if (!source)
		return std::nullopt;

	const auto id = source->GetGlyphId(element->GetDrawnCodepoint(codepoint));
	if (!id)
		return std::nullopt;

	const auto elementPx = element->GetPx(px);
	const uint32_t ids[]{id};
	const float advances[]{source->GetAdvance(elementPx, id)};
	const GlyphRun run{ids, advances};
	auto raw = source->Rasterize(elementPx, run);
	if (!raw)
		return std::nullopt;

	return element->Wrap(std::move(*raw), px, GetAlignment(px, game), *source, run);
}

std::unique_ptr<FontReplacement::IFaceSource> FontReplacement::MaterializedFont::CreateSource(const FontElement& def, FontServices& services, const std::string& fontName) {
	std::unique_ptr<IFaceSource> source;
	switch (def.Source) {
		// FontChanger's game font elements name a family read from the game's files; those made here name none and mean the replaced font.
		case ElementSourceEnum::Game:
			if (def.Lookup.FamilyName.empty())
				return nullptr;
			source = GameFontDataFaceSource::Create(def, fontName);
			if (!source)
				source = std::make_unique<EmptyFaceSource>(def.Size, def.RendererSpecific.Empty.Ascent, def.RendererSpecific.Empty.LineHeight);
			break;

		case ElementSourceEnum::DirectWrite:
		case ElementSourceEnum::FreeType:
			source = OutlineFaceSource::Create(services, def, fontName);
			if (!source)
				source = std::make_unique<EmptyFaceSource>(0.f, 0.f, 0.f);
			break;

		case ElementSourceEnum::GlyphImages:
			source = ImageFaceSource::Create(def);
			break;

		default:
			source = std::make_unique<EmptyFaceSource>(def.Size, def.RendererSpecific.Empty.Ascent, def.RendererSpecific.Empty.LineHeight);
			break;
	}

	// As in FontChanger, glyphs are merged only around fonts that draw outlines or images.
	if (!def.GlyphMerging.IsEnabled() || (def.Source != ElementSourceEnum::DirectWrite && def.Source != ElementSourceEnum::FreeType && def.Source != ElementSourceEnum::GlyphImages))
		return source;
	return MergingFaceSource::Create(std::move(source), def, services, fontName);
}

FontReplacement::LineMetrics FontReplacement::MaterializedFont::GetElementMetrics(const MaterializedFontElement& element, float px, GameFont* game) {
	if (!element.DrawsGame())
		return element.GetLineMetrics(px);
	if (!game || game->Size() <= 0)
		return {};
	const auto scale = element.GetPx(px) / game->Size();
	return {static_cast<int>(std::round(static_cast<float>(game->Ascent()) * scale)), static_cast<int>(std::round(static_cast<float>(game->LineHeight()) * scale))};
}

bool FontReplacement::MaterializedFont::Has(const MaterializedFontElement& element, std::span<const char32_t> grapheme, GameFont* game) {
	for (const auto codepoint : grapheme) {
		const auto has = element.DrawsGame()
			? element.Contains(codepoint) && game && game->HasGlyph(codepoint)
			: element.Has(codepoint);
		if (!has)
			return false;
	}
	return true;
}

size_t FontReplacement::MaterializedFont::CountOwnCodepoints() const {
	std::vector<bool> drawn(0x110000);
	size_t count = 0;
	for (const auto& e : m_elements) {
		const auto source = e->Source();
		if (!source)
			continue;
		for (const auto c : source->Codepoints()) {
			if (c < drawn.size() && !drawn[c] && e->Contains(c)) {
				drawn[c] = true;
				count++;
			}
		}
	}
	return count;
}

FontReplacement::MaterializedFontElement* FontReplacement::MaterializedFont::FindElement(std::span<const char32_t> grapheme, GameFont* game) {
	if (const auto assigned = FindAssigned(grapheme, game))
		return assigned;
	return m_systemFallback ? GetFallbackElement(grapheme) : m_gameElement.get();
}

FontReplacement::MaterializedFontElement* FontReplacement::MaterializedFont::FindAssigned(std::span<const char32_t> grapheme, GameFont* game) {
	MaterializedFontElement* assigned = nullptr;
	for (const auto& e : m_elements) {
		const auto mode = e->Def().MergeMode;
		if ((mode == codepoint_merge_mode::AddNew && assigned) || (mode == codepoint_merge_mode::Replace && !assigned))
			continue;
		if (Has(*e, grapheme, game))
			assigned = e.get();
	}
	return assigned;
}

FontReplacement::MaterializedFontElement* FontReplacement::MaterializedFont::GetFallbackElement(std::span<const char32_t> grapheme) {
	std::wstring text;
	for (const auto c : grapheme) {
		if (c >= 0x10000) {
			text.push_back(static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10)));
			text.push_back(static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
		} else {
			text.push_back(static_cast<wchar_t>(c));
		}
	}

	const auto primary = m_primary ? &m_primary->Def() : nullptr;
	const auto baseFamily = primary ? std::string_view(primary->Lookup.FamilyName) : std::string_view(SystemFallbackFonts::DefaultBaseFamily);
	const auto found = m_fallbackFonts.Find(text, baseFamily);
	if (!found)
		return nullptr;
	const auto& [lookup, scale] = *found;

	const auto& fitted = GetFittedFallback(lookup);
	for (auto it = fitted.rbegin(); it != fitted.rend(); ++it) {
		if ((*it)->Has(grapheme[0]))
			return it->get();
	}

	// Private-use characters, and fonts that aren't a game font's, keep DirectWrite's scale.
	auto name = lookup.FamilyName;
	std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	const auto key = std::make_tuple(std::move(name), static_cast<int>(lookup.Weight), static_cast<int>(lookup.Stretch), static_cast<int>(lookup.Style), scale);
	auto it = m_fallbackElements.find(key);
	if (it == m_fallbackElements.end()) {
		FontElement def;
		def.Size = (primary ? primary->Size : m_referenceSize) * scale;
		def.Source = ElementSourceEnum::DirectWrite;
		def.Lookup = lookup;
		def.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
		if (primary)
			def.RendererSpecific.DirectWrite = primary->RendererSpecific.DirectWrite;

		// Rendered as the fallback font's own bitmaps and gasp table want at its size, not as the primary element's.
		if (const auto info = FontChanger::FontsFromSystemFont::FontInfo::Open(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style))
			def.RendererSpecific.DirectWrite.RenderMode = FontChanger::FontsFromSystemFont::GetRenderMode(*info, def.Size);
		auto source = OutlineFaceSource::Create(m_services, def, m_def->Name);
		if (source)
			source->SetMeasureMode(m_measureMode);
		it = m_fallbackElements.emplace(key, std::make_unique<MaterializedFontElement>(std::move(def), std::move(source), m_referenceSize)).first;
	}

	const auto& element = *it->second;
	return element.Source() && element.Has(grapheme[0]) ? it->second.get() : nullptr;
}

const std::vector<std::unique_ptr<FontReplacement::MaterializedFontElement>>& FontReplacement::MaterializedFont::GetFittedFallback(const FontChanger::Structs::LookupStruct& lookup) {
	auto name = lookup.FamilyName;
	std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	const auto key = std::make_tuple(std::move(name), static_cast<int>(lookup.Weight), static_cast<int>(lookup.Stretch), static_cast<int>(lookup.Style));
	if (const auto it = m_fittedFallback.find(key); it != m_fittedFallback.end())
		return it->second;

	auto& fitted = m_fittedFallback[key];
	try {
		const auto names = FontNames::FontNamesOf(Presets::FamilyOf(m_def->Name));
		const auto game = std::ranges::find_if(names, [this](const auto& f) { return _stricmp(f.first.c_str(), m_def->Name.c_str()) == 0; });
		if (game == names.end() || game->second <= 0)
			return fitted;

		const FontChanger::FontsFromSystemFont::FontOpener open = [this](const FontChanger::Structs::LookupStruct& l) { return OpenProbe(l); };
		if (!m_gameProbe)
			m_gameProbe = GameFontDataFaceSource::OpenMetrics(m_def->Name);
		if (!m_drawnProbe)
			m_drawnProbe = FontChanger::FontsFromSystemFont::ProbeFont(*m_def, open);
		for (auto& e : FontChanger::FontsFromSystemFont::MakeFallbackElements(*m_def, game->second, lookup, open, *m_drawnProbe, m_gameProbe->get())) {
			auto source = CreateSource(*e, m_services, m_def->Name);
			if (source)
				source->SetMeasureMode(m_measureMode);
			fitted.emplace_back(std::make_unique<MaterializedFontElement>(std::move(*e), std::move(source), m_referenceSize));
		}
	} catch (const std::exception& e) {
		Host::Error("Fitting {} for {} failed; it is drawn at DirectWrite's scale: {}", lookup.FamilyName, m_def->Name, e.what());
		fitted.clear();
	}
	return fitted;
}

const FontChanger::FontsFromSystemFont::FontInfo* FontReplacement::MaterializedFont::OpenProbe(const FontChanger::Structs::LookupStruct& lookup) {
	const auto key = std::make_tuple(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style);
	auto it = m_probes.find(key);
	if (it == m_probes.end())
		it = m_probes.emplace(key, FontChanger::FontsFromSystemFont::FontInfo::Open(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style)).first;
	return it->second ? &*it->second : nullptr;
}
