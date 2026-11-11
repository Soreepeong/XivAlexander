#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/FontFileKey.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/FreeTypeGlyphRasterizer.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::FontElement;
using FontChanger::Structs::ElementSourceEnum;

FontReplacement::OutlineFaceSource::OutlineFaceSource(FontServices& services, FontElement def, std::unique_ptr<LookupFont> font, FT_FaceRec_* freeTypeFace, std::string fontName)
	: m_services(services)
	, m_def(std::move(def))
	, m_font(std::move(font))
	, m_freeTypeFace(freeTypeFace)
	, m_fontName(std::move(fontName))
	, m_transform(Matrix2x2::Of(m_def.Transform) * m_font->Transform()) {
	if (m_def.Gamma != 1 && m_def.Gamma > 0)
		m_gamma = RasterGlyph::CoverageTable(1 / m_def.Gamma);
}

FontReplacement::OutlineFaceSource::~OutlineFaceSource() = default;

std::unique_ptr<FontReplacement::OutlineFaceSource> FontReplacement::OutlineFaceSource::Create(FontServices& services, const FontElement& def, const std::string& fontName) {
	auto font = LookupFont::Create(services, def.Lookup, def.Source);
	if (!font) {
		Host::Warning("{}: font {} not found", fontName, def.Lookup.FamilyName);
		return nullptr;
	}

	return FromFont(services, def, std::move(font), fontName);
}

std::unique_ptr<FontReplacement::OutlineFaceSource> FontReplacement::OutlineFaceSource::FromFont(FontServices& services, FontElement def, std::unique_ptr<LookupFont> font, const std::string& fontName) {
	// DirectWrite doesn't draw fonts with only bitmaps.
	const auto ftFace = services.FreeType().Open(font->Face());
	const auto useFreeType = ftFace && (def.Source == ElementSourceEnum::FreeType || !FreeTypeGlyphRasterizer::IsScalable(ftFace));
	if (!useFreeType && def.Source == ElementSourceEnum::FreeType)
		Host::Warning("{} can't be opened with FreeType; it is drawn with DirectWrite", def.Lookup.FamilyName);
	return std::unique_ptr<OutlineFaceSource>(new OutlineFaceSource(services, std::move(def), std::move(font), useFreeType ? ftFace : nullptr, fontName));
}

void FontReplacement::OutlineFaceSource::SetMeasureMode(DWRITE_MEASURING_MODE value) {
	m_measureMode = value;
	if (m_italic)
		m_italic->SetMeasureMode(value);
	if (m_bold)
		m_bold->SetMeasureMode(value);
}

const std::set<char32_t>& FontReplacement::OutlineFaceSource::Codepoints() {
	if (m_codepoints)
		return *m_codepoints;

	m_codepoints.emplace();
	const auto face = m_font->Face();
	UINT32 count = 0;
	face->GetUnicodeRanges(0, nullptr, &count);
	std::vector<DWRITE_UNICODE_RANGE> ranges(count);
	if (SUCCEEDED(face->GetUnicodeRanges(count, ranges.data(), &count))) {
		for (const auto& r : std::span(ranges).first(count)) {
			// Ranges may span surrogates, which aren't codepoints.
			for (auto c = r.first; c <= r.last; c++) {
				if (c < 0xD800 || (c >= 0xE000 && c <= 0x10FFFF))
					m_codepoints->insert(static_cast<char32_t>(c));
			}
		}
	}
	return *m_codepoints;
}

uint32_t FontReplacement::OutlineFaceSource::GetGlyphId(char32_t codepoint) {
	const auto cp = static_cast<UINT32>(codepoint);
	uint16_t index;
	return SUCCEEDED(m_font->Face()->GetGlyphIndices(&cp, 1, &index)) ? index : 0u;
}

float FontReplacement::OutlineFaceSource::GetAdvance(float px, uint32_t glyphId) {
	const auto index = static_cast<uint16_t>(glyphId);
	DWRITE_GLYPH_METRICS gm;
	if (FAILED(m_font->GetFace(px)->GetDesignGlyphMetrics(&index, 1, &gm, FALSE)))
		return 0;
	return static_cast<float>(gm.advanceWidth) * px / static_cast<float>(m_font->Metrics().designUnitsPerEm);
}

float FontReplacement::OutlineFaceSource::GetScreenAdvance(float px, const GlyphRun& run) {
	auto sum = 0.f;
	for (const auto a : run.Advances)
		sum += a;
	return (sum + GetExtraAdvance(px) * static_cast<float>(run.GlyphIds.size())) * m_transform.M11;
}

FontReplacement::LineMetrics FontReplacement::OutlineFaceSource::GetLineMetrics(float px, const Matrix2x2& transform) const {
	const auto& m = m_font->Metrics();
	const auto size = px / static_cast<float>(m.designUnitsPerEm) * std::abs(transform.M22);
	const auto ascent = static_cast<int>(std::ceil(static_cast<float>(m.ascent) * size));
	const auto descent = static_cast<int>(std::ceil(static_cast<float>(m.descent) * size));
	return {ascent, ascent + descent + static_cast<int>(std::round(static_cast<float>(m.lineGap) * size))};
}

std::pair<FontReplacement::IFaceSource*, bool> FontReplacement::OutlineFaceSource::GetItalic() {
	if (IsSlanted())
		return {this, true};

	if (!m_italicMade) {
		m_italicMade = true;
		auto def = m_def;
		def.Lookup.Style = DWRITE_FONT_STYLE_ITALIC;
		m_italic = MakeVariant(std::move(def), [this](const LookupFont& f) { return IsRealItalic(f.Face()) && !SameFont(f.Face(), m_font->Face()); }, "italic");
	}

	if (m_italic)
		return {m_italic.get(), true};
	return {this, false};
}

FontReplacement::IFaceSource* FontReplacement::OutlineFaceSource::GetBold() {
	if (!m_boldMade) {
		m_boldMade = true;
		auto def = m_def;
		const auto weight = def.Lookup.Weight;
		def.Lookup.Weight = weight < 350 ? DWRITE_FONT_WEIGHT_NORMAL : weight < 550 ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_BLACK;

		// FreeType draws none of DirectWrite's simulations, so it emboldens by synthesis.
		if (!def.Lookup.Synthesis && def.Source == ElementSourceEnum::FreeType)
			def.Lookup.Synthesis = FontChanger::Structs::SynthesisStruct{.Allow = true};
		m_bold = MakeVariant(std::move(def), [this](const LookupFont& f) { return IsBolder(f); }, "bold");
	}

	return m_bold ? m_bold.get() : this;
}

std::unique_ptr<FontReplacement::OutlineFaceSource> FontReplacement::OutlineFaceSource::MakeVariant(FontElement def, const std::function<bool(const LookupFont&)>& differs, std::string_view what) {
	try {
		if (auto font = LookupFont::Create(m_services, def.Lookup, def.Source); font && differs(*font)) {
			auto made = FromFont(m_services, std::move(def), std::move(font), m_fontName);
			made->SetMeasureMode(m_measureMode);
			return made;
		}
	} catch (const std::exception& e) {
		Host::Warning("{}: the {} of {} can't be made: {}", m_fontName, what, m_def.Lookup.FamilyName, e.what());
	}
	return nullptr;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::OutlineFaceSource::Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options) {
	const auto count = run.GlyphIds.size();
	if (count == 0)
		return std::nullopt;

	const auto extra = GetExtraAdvance(px);
	std::vector<uint16_t> glyphs(count);
	std::vector<float> advances(count);
	for (size_t i = 0; i < count; i++) {
		glyphs[i] = static_cast<uint16_t>(run.GlyphIds[i]);
		advances[i] = run.Advances[i] + extra;
	}

	std::vector<DWRITE_GLYPH_OFFSET> offsets;
	if (!run.Offsets.empty()) {
		offsets.resize(count);
		for (size_t i = 0; i < count && i < run.Offsets.size(); i++)
			offsets[i] = {run.Offsets[i].Advance, run.Offsets[i].Ascender};
	}

	const auto advance = options.Advance.value_or(static_cast<int>(std::round(GetScreenAdvance(px, run))));
	return RasterizeRun(
		px,
		glyphs.data(),
		advances.data(),
		offsets.empty() ? nullptr : offsets.data(),
		static_cast<uint32_t>(count),
		options.OriginX,
		advance,
		Matrix2x2::CreateScale(options.SqueezeX, 1) * m_transform);
}

FontReplacement::RasterGlyph FontReplacement::OutlineFaceSource::RasterizeRun(
	float size,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	const Matrix2x2& transform,
	float originY) {
	RasterGlyph glyph;
	if (m_freeTypeFace) {
		const auto axes = m_font->GetAxisValues(size);
		glyph = m_services.FreeType().RasterizeRun(
			m_freeTypeFace, size, glyphs, advances, offsets, count, originX, advance, FreeTypeParams::Of(m_def.RendererSpecific.FreeType), transform, &axes,
			m_font->Embolden(), originY);
	} else {
		auto parameters = DirectWriteParams::Of(m_def.RendererSpecific.DirectWrite);
		parameters.MeasureMode = m_measureMode;
		glyph = m_services.DirectWrite().RasterizeRun(m_font->GetFace(size), size, glyphs, advances, offsets, count, originX, advance, parameters, transform, originY);
	}
	return m_gamma ? glyph.WithCoverage(*m_gamma) : glyph;
}

float FontReplacement::OutlineFaceSource::GetExtraAdvance(float px) const {
	return m_freeTypeFace ? m_font->Embolden() * px : 0;
}

bool FontReplacement::OutlineFaceSource::IsRealItalic(IDWriteFontFace5* face) {
	return (face->GetSimulations() & DWRITE_FONT_SIMULATIONS_OBLIQUE) == 0 && face->GetStyle() != DWRITE_FONT_STYLE_NORMAL;
}

bool FontReplacement::OutlineFaceSource::IsSlanted() {
	if (!m_isSlanted) {
		const auto face = m_font->Face();
		m_isSlanted = IsRealItalic(face) || (face->GetSimulations() & DWRITE_FONT_SIMULATIONS_OBLIQUE);
	}
	return *m_isSlanted;
}

bool FontReplacement::OutlineFaceSource::IsBolder(const LookupFont& font) const {
	const auto face = font.Face();
	const auto own = m_font->Face();
	return face->GetWeight() > own->GetWeight()
		|| ((face->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD) && !(own->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD))
		|| font.Embolden() > m_font->Embolden();
}

bool FontReplacement::OutlineFaceSource::SameFont(IDWriteFontFace5* a, IDWriteFontFace5* b) {
	if (a == b)
		return true;
	if (a->GetIndex() != b->GetIndex() || a->GetGlyphCount() != b->GetGlyphCount())
		return false;
	return FontFileKey::Of(a).SameFileAs(FontFileKey::Of(b));
}
