#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"

#include <FontChanger.Presets/CodepointRanges.h>

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using namespace FontChanger::FixedSizeFont;

FontReplacement::MaterializedFontElement::MaterializedFontElement(FontChanger::Structs::FontElement def, std::unique_ptr<IFaceSource> source, float referenceSize)
	: m_def(std::move(def))
	, m_source(std::move(source))
	, m_referenceSize(referenceSize) {
}

FontReplacement::MaterializedFontElement::~MaterializedFontElement() = default;

bool FontReplacement::MaterializedFontElement::Contains(char32_t codepoint) const {
	return FontChanger::CodepointRanges::Contains(m_def.WrapModifiers.Codepoints, codepoint);
}

bool FontReplacement::MaterializedFontElement::Has(char32_t codepoint) const {
	return Contains(codepoint) && m_source && m_source->GetGlyphId(GetDrawnCodepoint(codepoint)) != 0;
}

char32_t FontReplacement::MaterializedFontElement::GetDrawnCodepoint(char32_t codepoint) const {
	const auto& replacements = m_def.WrapModifiers.CodepointReplacements;
	const auto it = replacements.find(codepoint);
	return it == replacements.end() ? codepoint : it->second;
}

FontReplacement::LineMetrics FontReplacement::MaterializedFontElement::GetLineMetrics(float fontPx) const {
	return m_source ? m_source->GetLineMetrics(GetPx(fontPx)) : LineMetrics{};
}

float FontReplacement::MaterializedFontElement::GetRomanBaselineY(float fontPx, int ascent) const {
	const auto y = m_source ? m_source->TryGetBaseline(GetPx(fontPx), Baseline::Roman) : std::nullopt;
	return y ? static_cast<float>(ascent) - *y : static_cast<float>(ascent);
}

float FontReplacement::MaterializedFontElement::GetIdeographicCenterY(float fontPx, int ascent, int lineHeight) const {
	if (m_source) {
		const auto px = GetPx(fontPx);
		if (const auto bottom = m_source->TryGetBaseline(px, Baseline::IdeographicFaceBottom)) {
			if (const auto top = m_source->TryGetBaseline(px, Baseline::IdeographicFaceTop))
				return static_cast<float>(ascent) - (*bottom + *top) / 2;
		}
		if (const auto bottom = m_source->TryGetBaseline(px, Baseline::IdeographicBottom)) {
			if (const auto top = m_source->TryGetBaseline(px, Baseline::IdeographicTop))
				return static_cast<float>(ascent) - (*bottom + *top) / 2;
		}
	}
	return static_cast<float>(lineHeight) / 2;
}

FontReplacement::RasterGlyph FontReplacement::MaterializedFontElement::Wrap(RasterGlyph glyph, float fontPx, const FontAlignment& font, IFaceSource& source, const GlyphRun& run, float originX) {
	auto g = Monospace(fontPx, std::move(glyph), source, run, originX);
	g.Advance += GetLetterSpacing(fontPx);
	g.Left += ScalePixels(m_def.WrapModifiers.HorizontalOffset, fontPx);
	g.Top += GetVerticalShift(fontPx, font);
	return g;
}

int FontReplacement::MaterializedFontElement::GetVerticalShift(float fontPx, const FontAlignment& font) const {
	const auto [ascent, lineHeight] = GetLineMetrics(fontPx);
	int adjustment;
	switch (font.Mode) {
		case vertical_alignment::Top:
			adjustment = 0;
			break;
		case vertical_alignment::Middle:
			adjustment = (font.LineHeight - lineHeight) / 2;
			break;
		case vertical_alignment::Bottom:
			adjustment = font.LineHeight - lineHeight;
			break;
		case vertical_alignment::RomanBaseline:
			adjustment = static_cast<int>(std::round(font.RomanBaselineY - GetRomanBaselineY(fontPx, ascent)));
			break;
		case vertical_alignment::IdeographicCenter:
			adjustment = static_cast<int>(std::round(font.IdeographicCenterY - GetIdeographicCenterY(fontPx, ascent, lineHeight)));
			break;
		default:
			adjustment = font.Ascent - ascent;
			break;
	}
	return ascent + adjustment - font.Ascent + ScalePixels(m_def.WrapModifiers.VerticalOffset, fontPx);
}

FontReplacement::RasterGlyph FontReplacement::MaterializedFontElement::Monospace(float fontPx, RasterGlyph glyph, IFaceSource& source, const GlyphRun& run, float originX) {
	const auto& mono = m_def.WrapModifiers.Monospacing;
	if (!mono.is_enabled() || glyph.Advance <= 0)
		return glyph;

	float unit;
	switch (mono.Unit) {
		case monospacing_unit::Pixels:
			unit = fontPx / m_referenceSize;
			break;
		case monospacing_unit::Em:
			unit = GetPx(fontPx);
			break;
		default:
			unit = GetReferenceAdvance(fontPx, mono.ReferenceCharacter);
			break;
	}
	if (!(unit > 0))
		return glyph;

	auto min = mono.MinAdvance ? (std::max)(0, static_cast<int>(std::round(*mono.MinAdvance * unit))) : 0;
	const auto max = mono.MaxAdvance ? (std::max)(0, static_cast<int>(std::round(*mono.MaxAdvance * unit))) : INT_MAX;
	min = (std::min)(min, max);
	const auto cell = std::clamp(glyph.Advance, min, max);
	if (glyph.Width == 0) {
		glyph.Advance = cell;
		return glyph;
	}

	int left;
	if (mono.MaxAdvance && cell > 0 && glyph.Width > cell) {
		// Hinting and rounding make the ink only roughly as narrow as the scale says, so narrower scales are tried.
		auto scale = static_cast<float>(cell) / static_cast<float>(glyph.Width);
		const auto px = GetPx(fontPx);
		auto fitted = glyph;
		for (auto attempt = 0; attempt < 8; attempt++) {
			auto squeezed = source.Rasterize(px, run, {.OriginX = originX, .SqueezeX = scale});
			fitted = squeezed ? std::move(*squeezed) : glyph;
			if (fitted.Width <= cell)
				break;
			scale *= static_cast<float>(cell) / static_cast<float>(fitted.Width) * 0.98f;
		}

		left = (cell - fitted.Width) >> 1;
		glyph = std::move(fitted);
	} else {
		switch (mono.Alignment) {
			case monospacing_alignment::Left:
				left = glyph.Left;
				break;
			case monospacing_alignment::CenterInk:
				left = (cell - glyph.Width) >> 1;
				break;
			case monospacing_alignment::Right:
				left = glyph.Left + cell - glyph.Advance;
				break;
			default:
				left = glyph.Left + ((cell - glyph.Advance) >> 1);
				break;
		}
	}

	// Wrap adds the horizontal offset afterwards; it may nudge the ink in its cell, but not left of the pen.
	const auto offset = ScalePixels(m_def.WrapModifiers.HorizontalOffset, fontPx);
	glyph.Advance = cell;
	glyph.Left = (std::max)(0, left + offset) - offset;
	return glyph;
}

float FontReplacement::MaterializedFontElement::GetReferenceAdvance(float fontPx, char32_t codepoint) {
	if (const auto it = m_referenceAdvances.find(fontPx); it != m_referenceAdvances.end())
		return it->second;

	auto advance = 0.f;
	if (m_source) {
		if (const auto id = m_source->GetGlyphId(codepoint)) {
			const auto px = GetPx(fontPx);
			const uint32_t ids[]{id};
			const float advances[]{m_source->GetAdvance(px, id)};
			advance = m_source->GetScreenAdvance(px, {ids, advances});
		}
	}

	m_referenceAdvances.emplace(fontPx, advance);
	return advance;
}
