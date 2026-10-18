#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"

#include <FontChanger.Presets/FontsFromSystemFont.h>

#include "MainApp/FontReplacement/FontStructs.h"
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

	// A font that is installed, with glyph merging around it or not.
	for (const auto& e : m_elements) {
		const auto renderer = e->Def().Source;
		if ((renderer == ElementSourceEnum::DirectWrite || renderer == ElementSourceEnum::FreeType) && !dynamic_cast<EmptyFaceSource*>(e->Source())) {
			m_primary = e.get();
			break;
		}
	}
	m_measureMode = m_primary ? m_primary->Def().RendererSpecific.DirectWrite.MeasureMode : DWRITE_MEASURING_MODE_NATURAL;

	// The font's text is measured one way, its primary element's; every element draws glyphs measured so.
	for (const auto& e : m_elements)
		SetMeasureMode(e->Source());
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

size_t FontReplacement::MaterializedFont::OwnCodepointCount() {
	if (m_ownCodepointCount)
		return *m_ownCodepointCount;

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
	m_ownCodepointCount = count;
	return count;
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

	// A grapheme goes to an element that has all of it, else to its first codepoint's.
	if (const auto assigned = FindAssigned(grapheme, game))
		return assigned;
	return TryGetElement(grapheme.first(1), game);
}

FontReplacement::LineMetrics FontReplacement::MaterializedFont::GetLineMetrics(float px, GameFont* game) {
	auto metrics = m_elements.empty() ? LineMetrics{} : GetElementMetrics(*m_elements[0], px, game);
	if (metrics.LineHeight <= 0) {
		// Nothing to take them from: proportions of Segoe UI.
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
		// FontChanger's game font elements name a family, drawn from the game's files (no glyphs if they can't be read); those made here name
		// none, and use the game font being replaced.
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

void FontReplacement::MaterializedFont::SetMeasureMode(IFaceSource* source) const {
	if (const auto outline = dynamic_cast<OutlineFaceSource*>(source))
		outline->SetMeasureMode(m_measureMode);
	else if (const auto merging = dynamic_cast<MergingFaceSource*>(source)) {
		for (const auto o : merging->OutlineSources())
			o->SetMeasureMode(m_measureMode);
	}
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

		// Drawn as the fallback font itself is to be at its size (its bitmaps, and its gasp table), as the fonts made from
		// system fonts are, rather than as the primary element's font is.
		if (const auto info = FontChanger::FontsFromSystemFont::FontInfo::Open(lookup.FamilyName, lookup.Weight, lookup.Stretch, lookup.Style))
			def.RendererSpecific.DirectWrite.RenderMode = FontChanger::FontsFromSystemFont::GetRenderMode(*info, def.Size);
		auto source = OutlineFaceSource::Create(m_services, def, m_def->Name);
		SetMeasureMode(source.get());
		it = m_fallbackElements.emplace(key, std::make_unique<MaterializedFontElement>(std::move(def), std::move(source), m_referenceSize)).first;
	}

	const auto& element = *it->second;
	return element.Source() && element.Has(grapheme[0]) ? it->second.get() : nullptr;
}
