#include "pch.h"
#include "MainApp/FontReplacement/ReplacementFace.h"

#include <FontChanger.FixedSizeFont/util.truetype.h>
#include <FontChanger.Presets/CodepointRanges.h>

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::RendererEnum;
using namespace FontChanger::FixedSizeFont;

namespace {
	// The game's icon font: private-use characters go to it before the system fallback.
	constexpr auto IconFamily = "XIV AXIS Std ATK";

	// The built-in face: Segoe UI, the game's icon font, then fonts for scripts Segoe UI lacks.
	constexpr const char* BuiltInFamilies[]{
		"Segoe UI", IconFamily, "Yu Gothic UI", "Malgun Gothic", "Microsoft YaHei UI", "Microsoft JhengHei UI",
		"Nirmala UI", "Leelawadee UI", "Segoe UI Symbol", "Segoe UI Emoji",
	};
}

bool FontReplacement::FaceElement::Contains(char32_t codepoint) const {
	return FontChanger::CodepointRanges::Contains(m_def.WrapModifiers.Codepoints, codepoint);
}

FontReplacement::ReplacementFace::ReplacementFace(GlyphRasterizer& rasterizer, std::shared_ptr<const FontChanger::Structs::Face> def, bool systemFallback, ReplacementFace* fallback)
	: m_rasterizer(rasterizer)
	, m_def(std::move(def))
	, m_fallback(systemFallback ? fallback : nullptr)
	, m_systemFallback(systemFallback) {
	m_gameElementDef.Renderer = RendererEnum::PrerenderedGameInstallation;
	m_gameElement = std::make_unique<FaceElement>(m_gameElementDef, std::make_unique<GameElementFont>());

	for (const auto& e : m_def->Elements)
		m_elements.push_back(std::make_unique<FaceElement>(*e, CreateFont(*e, rasterizer, m_def->Name)));

	m_referenceSize = !m_def->Elements.empty() && m_def->Elements[0]->Size > 0 ? m_def->Elements[0]->Size : 1.f;
	if (const auto it = std::ranges::find_if(m_elements, [](const auto& e) { return e->Shaped() != nullptr; }); it != m_elements.end())
		m_primary = it->get();
	m_measureMode = m_primary ? m_primary->Def().RendererSpecific.DirectWrite.MeasureMode : DWRITE_MEASURING_MODE_NATURAL;

	// The face's text is measured one way, its primary element's; every font of it draws glyphs measured so.
	for (const auto& e : m_elements) {
		for (const auto outline : OutlineFonts(e->Font()))
			outline->MeasureMode = m_measureMode;
	}
}

std::unique_ptr<FontReplacement::ReplacementFace> FontReplacement::ReplacementFace::CreateBuiltIn(GlyphRasterizer& rasterizer) {
	auto face = std::make_shared<FontChanger::Structs::Face>();
	face->Name = "(built-in)";
	for (const auto family : BuiltInFamilies) {
		auto& e = *face->Elements.emplace_back(std::make_unique<FontChanger::Structs::FaceElement>());
		e.Size = 1;
		e.Renderer = RendererEnum::DirectWrite;
		e.Lookup.Name = family;
		e.WrapModifiers.Codepoints = {{0x20, 0x10FFFF}};
	}
	return std::make_unique<ReplacementFace>(rasterizer, std::move(face), true, nullptr);
}

std::unique_ptr<FontReplacement::ReplacementFace> FontReplacement::ReplacementFace::CreateGame(GlyphRasterizer& rasterizer, bool systemFallback, ReplacementFace* fallback) {
	auto face = std::make_shared<FontChanger::Structs::Face>();
	face->Name = "(game)";
	auto& e = *face->Elements.emplace_back(std::make_unique<FontChanger::Structs::FaceElement>());
	e.Size = 1;
	e.Renderer = RendererEnum::PrerenderedGameInstallation;
	e.WrapModifiers.Codepoints = {{0, 0x10FFFF}};
	return std::make_unique<ReplacementFace>(rasterizer, std::move(face), systemFallback, fallback);
}

float FontReplacement::ReplacementFace::GetElementPx(const FaceElement& element, float px) const {
	return px * element.Def().Size / m_referenceSize;
}

int FontReplacement::ReplacementFace::ScalePixels(float value, float px) const {
	return static_cast<int>(std::round(value * px / m_referenceSize));
}

FontReplacement::FaceElement* FontReplacement::ReplacementFace::GetElement(char32_t codepoint, GameFont* game) {
	if (const auto it = m_elementCache.find({game, codepoint}); it != m_elementCache.end())
		return it->second;

	FaceElement* assigned = nullptr;
	for (const auto& e : m_elements) {
		// Asked only of elements that would take it.
		const auto mode = e->Def().MergeMode;
		if ((mode == codepoint_merge_mode::AddNew && assigned) || (mode == codepoint_merge_mode::Replace && !assigned))
			continue;
		if (e->Contains(codepoint) && e->Font().Has(codepoint, game))
			assigned = e.get();
	}

	if (!assigned && !m_systemFallback)
		assigned = m_gameElement.get();
	m_elementCache.emplace(std::make_pair(game, codepoint), assigned);
	return assigned;
}

char32_t FontReplacement::ReplacementFace::GetDrawnCodepoint(const FaceElement& element, char32_t codepoint) {
	const auto& replacements = element.Def().WrapModifiers.CodepointReplacements;
	const auto it = replacements.find(codepoint);
	return it == replacements.end() ? codepoint : it->second;
}

FontReplacement::LineMetrics FontReplacement::ReplacementFace::GetLineMetrics(float px, GameFont* game) {
	auto metrics = m_elements.empty() ? LineMetrics{} : GetElementMetrics(*m_elements[0], px, game);
	if (metrics.LineHeight <= 0) {
		// Nothing to take them from: proportions of Segoe UI.
		metrics.Ascent = static_cast<int>(std::ceil(px * 1.08f));
		metrics.LineHeight = static_cast<int>(std::ceil(px * 1.33f));
	}
	return metrics;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::ReplacementFace::TryRasterize(char32_t codepoint, float px, GameFont* game) {
	const auto element = GetElement(codepoint, game);
	if (!element) {
		// At the size the face's text is laid out in.
		const auto size = m_primary ? GetElementPx(*m_primary, px) : px;
		return m_fallback ? m_fallback->TryRasterize(codepoint, size, nullptr) : std::nullopt;
	}

	auto& font = element->Font();
	const auto elementPx = GetElementPx(*element, px);
	const auto drawn = GetDrawnCodepoint(*element, codepoint);
	auto raw = font.Rasterize(drawn, elementPx);
	if (!raw)
		return std::nullopt;

	const auto& rawGlyph = *raw;
	return Wrap(*element, rawGlyph, px, game, [&](float scale) { return font.Squeeze(drawn, elementPx, rawGlyph, scale); });
}

FontReplacement::RasterGlyph FontReplacement::ReplacementFace::Wrap(const FaceElement& element, RasterGlyph glyph, float px, GameFont* game, const std::function<RasterGlyph(float)>& squeezed) {
	auto g = Monospace(element, px, std::move(glyph), squeezed);
	g.Advance += GetLetterSpacing(&element, px);
	g.Left += ScalePixels(element.Def().WrapModifiers.HorizontalOffset, px);
	g.Top += GetVerticalShift(element, px, game);
	return g;
}

int FontReplacement::ReplacementFace::GetLetterSpacing(const FaceElement* element, float px) const {
	return element ? ScalePixels(element->Def().WrapModifiers.LetterSpacing, px) : 0;
}

FontReplacement::RasterGlyph FontReplacement::ReplacementFace::RasterizeRun(
	const FaceElement* element,
	IDWriteFontFace* face,
	float size,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	float squeezeX) const {
	if (const auto font = element ? element->Shaped() : nullptr)
		return font->RasterizeRun(face, size, glyphs, advances, offsets, count, originX, advance, squeezeX);
	DirectWriteParams parameters;
	parameters.MeasureMode = m_measureMode;
	return m_rasterizer.RasterizeRun(face, size, glyphs, advances, offsets, count, originX, advance, parameters, GlyphTransform::Identity().ScaledX(squeezeX));
}

std::unique_ptr<FontReplacement::IElementFont> FontReplacement::ReplacementFace::CreateFont(const FontChanger::Structs::FaceElement& def, GlyphRasterizer& rasterizer, const std::string& faceName) {
	// FontChanger's gamma: coverage to the power of 1 / gamma.
	std::optional<std::vector<uint8_t>> gamma;
	if (def.Gamma != 1 && def.Gamma > 0)
		gamma = RasterGlyph::CoverageTable(1 / def.Gamma);

	std::unique_ptr<IElementFont> font;
	switch (def.Renderer) {
		case RendererEnum::PrerenderedGameInstallation:
			font = std::make_unique<GameElementFont>();
			break;

		case RendererEnum::DirectWrite:
		case RendererEnum::FreeType:
			font = OutlineElementFont::Create(rasterizer, def, def.Renderer, gamma, faceName);
			if (!font)
				font = std::make_unique<EmptyElementFont>(0.f, 0.f, 0.f);
			break;

		case RendererEnum::GlyphImages:
			// Drawn by xivres's image fonts, which apply the gamma themselves.
			font = std::make_unique<ImageElementFont>(def.RendererSpecific.GlyphImages, def.Gamma, GlyphTransform::Of(def.Transform));
			break;

		default:
			font = std::make_unique<EmptyElementFont>(def.Size, def.RendererSpecific.Empty.Ascent, def.RendererSpecific.Empty.LineHeight);
			break;
	}

	if (!def.GlyphMerging.IsEnabled())
		return font;

	// Texts are drawn with the element's font; glyph images draw them with the font the lookup names, if any, else with
	// their own files.
	std::unique_ptr<OutlineElementFont> ownTextOutline;
	if (dynamic_cast<ImageElementFont*>(font.get()) && !def.Lookup.Name.empty())
		ownTextOutline = OutlineElementFont::Create(rasterizer, def, RendererEnum::DirectWrite, gamma, faceName);
	return std::make_unique<MergingElementFont>(rasterizer, std::move(font), def, std::move(ownTextOutline));
}

std::vector<FontReplacement::OutlineElementFont*> FontReplacement::ReplacementFace::OutlineFonts(IElementFont& font) {
	if (const auto outline = dynamic_cast<OutlineElementFont*>(&font))
		return {outline};
	if (const auto merging = dynamic_cast<MergingElementFont*>(&font))
		return merging->OutlineFonts();
	return {};
}

int FontReplacement::ReplacementFace::GetVerticalShift(const FaceElement& element, float px, GameFont* game) {
	// The element's line box is placed in the face's: its top moved down by the adjustment.
	const auto face = GetLineMetrics(px, game);
	const auto metrics = GetElementMetrics(element, px, game);
	int adjustment;
	switch (m_def->VerticalAlignment) {
		case vertical_alignment::Top:
			adjustment = 0;
			break;
		case vertical_alignment::Middle:
			adjustment = (face.LineHeight - metrics.LineHeight) / 2;
			break;
		case vertical_alignment::Bottom:
			adjustment = face.LineHeight - metrics.LineHeight;
			break;
		case vertical_alignment::RomanBaseline:
			adjustment = static_cast<int>(std::round(GetRomanBaselineY(px, game) - GetRomanBaselineY(element, px, game)));
			break;
		case vertical_alignment::IdeographicCenter:
			adjustment = static_cast<int>(std::round(GetIdeographicCenterY(px, game) - GetIdeographicCenterY(element, px, game)));
			break;
		default:
			adjustment = face.Ascent - metrics.Ascent;
			break;
	}
	return metrics.Ascent + adjustment - face.Ascent + ScalePixels(element.Def().WrapModifiers.BaselineShift, px);
}

float FontReplacement::ReplacementFace::GetRomanBaselineY(float px, GameFont* game) {
	const auto baseline = m_elements.empty() ? std::nullopt : GetBaseline(*m_elements[0], truetype::Base::RomanBaselineTag.NativeValue, px);
	return static_cast<float>(GetLineMetrics(px, game).Ascent) - baseline.value_or(0.f);
}

float FontReplacement::ReplacementFace::GetRomanBaselineY(const FaceElement& element, float px, GameFont* game) {
	return static_cast<float>(GetElementMetrics(element, px, game).Ascent) - GetBaseline(element, truetype::Base::RomanBaselineTag.NativeValue, px).value_or(0.f);
}

float FontReplacement::ReplacementFace::GetIdeographicCenterY(float px, GameFont* game) {
	const auto metrics = GetLineMetrics(px, game);
	return m_elements.empty() ? static_cast<float>(metrics.LineHeight) / 2 : GetIdeographicCenterY(*m_elements[0], px, metrics.Ascent, metrics.LineHeight);
}

float FontReplacement::ReplacementFace::GetIdeographicCenterY(const FaceElement& element, float px, GameFont* game) {
	const auto metrics = GetElementMetrics(element, px, game);
	return GetIdeographicCenterY(element, px, metrics.Ascent, metrics.LineHeight);
}

float FontReplacement::ReplacementFace::GetIdeographicCenterY(const FaceElement& element, float px, int ascent, int lineHeight) {
	if (const auto bottom = GetBaseline(element, truetype::Base::IdeographicFaceBottomTag.NativeValue, px)) {
		if (const auto top = GetBaseline(element, truetype::Base::IdeographicFaceTopTag.NativeValue, px))
			return static_cast<float>(ascent) - (*bottom + *top) / 2;
	}
	if (const auto bottom = GetBaseline(element, truetype::Base::IdeographicEmBoxBottomTag.NativeValue, px)) {
		if (const auto top = GetBaseline(element, truetype::Base::IdeographicEmBoxTopTag.NativeValue, px))
			return static_cast<float>(ascent) - (*bottom + *top) / 2;
	}
	return static_cast<float>(lineHeight) / 2;
}

std::optional<float> FontReplacement::ReplacementFace::GetBaseline(const FaceElement& element, uint32_t tag, float px) {
	return element.Font().GetBaseline(tag, GetElementPx(element, px));
}

FontReplacement::LineMetrics FontReplacement::ReplacementFace::GetElementMetrics(const FaceElement& element, float px, GameFont* game) {
	return element.Font().GetLineMetrics(GetElementPx(element, px), game);
}

FontReplacement::RasterGlyph FontReplacement::ReplacementFace::Monospace(const FaceElement& element, float px, RasterGlyph glyph, const std::function<RasterGlyph(float)>& squeezed) {
	const auto& mono = element.Def().WrapModifiers.Monospacing;
	if (!mono.is_enabled() || glyph.Advance <= 0)
		return glyph;

	float unit;
	switch (mono.Unit) {
		case monospacing_unit::Pixels:
			unit = px / m_referenceSize;
			break;
		case monospacing_unit::Em:
			unit = GetElementPx(element, px);
			break;
		default:
			unit = GetReferenceAdvance(element, px, mono.ReferenceCharacter);
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
		// Hinting and rounding make the ink only roughly as narrow as the scale says: narrower scales are tried until it
		// fits.
		auto scale = static_cast<float>(cell) / static_cast<float>(glyph.Width);
		auto fitted = glyph;
		for (auto attempt = 0; attempt < 8; attempt++) {
			fitted = squeezed(scale);
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

	// The horizontal offset (added after) nudges the ink in its cell, but not left of the pen.
	const auto offset = ScalePixels(element.Def().WrapModifiers.HorizontalOffset, px);
	glyph.Advance = cell;
	glyph.Left = (std::max)(0, left + offset) - offset;
	return glyph;
}

float FontReplacement::ReplacementFace::GetReferenceAdvance(const FaceElement& element, float px, char32_t codepoint) {
	const auto key = std::make_pair(&element, px);
	if (const auto it = m_referenceAdvances.find(key); it != m_referenceAdvances.end())
		return it->second;
	const auto advance = element.Font().GetAdvance(codepoint, GetElementPx(element, px));
	m_referenceAdvances.emplace(key, advance);
	return advance;
}
