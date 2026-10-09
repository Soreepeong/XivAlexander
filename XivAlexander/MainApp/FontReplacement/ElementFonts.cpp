#include "pch.h"
#include "MainApp/FontReplacement/ElementFonts.h"

#include <FontChanger.FixedSizeFont/opentype_positioning.h>
#include <FontChanger.FixedSizeFont/util.truetype.h>
#include <FontChanger.Presets/DirectWriteUtil.h>
#include <FontChanger.Presets/GlyphFiles.h>

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontReplacement::ThrowOnError;
using FontChanger::Structs::RendererEnum;
using namespace FontChanger::FixedSizeFont;

namespace {
	// Reads the baselines of a font's BASE table (read_baselines), in font units above the glyphs' origin.
	std::map<uint32_t, int> ReadBaselines(IDWriteFontFace* face) {
		const void* data;
		UINT32 size;
		void* context;
		BOOL exists;
		if (FAILED(face->TryGetFontTable(truetype::Base::DirectoryTableTag.NativeValue, &data, &size, &context, &exists)) || !exists)
			return {};
		const auto release = xivres::util::on_dtor([&] { face->ReleaseFontTable(context); });
		return read_baselines(std::span(static_cast<const char*>(data), size));
	}

	// Copies coverage into a buffer of fixed_size_font::draw, keeping the larger value: x and y are where the coverage's top
	// left goes.
	void BlitInto(uint8_t* buffer, size_t stride, int destWidth, int destHeight, const FontReplacement::RasterGlyph& g, int x, int y) {
		for (auto row = (std::max)(0, -y); row < g.Height && row + y < destHeight; row++) {
			for (auto col = (std::max)(0, -x); col < g.Width && col + x < destWidth; col++) {
				auto& d = buffer[(static_cast<size_t>(row + y) * destWidth + col + x) * stride];
				d = (std::max)(d, g.Alpha[static_cast<size_t>(row) * g.Width + col]);
			}
		}
	}

	// An element's font at a size as a fixed_size_font, which glyph merging takes as its base font: its line metrics and
	// baselines, and its glyph of a codepoint for shapes that are the element's own glyph.
	class ElementBaseFont final : public default_abstract_fixed_size_font {
		FontReplacement::IElementFont& m_font;
		float m_px;
		FontReplacement::LineMetrics m_metrics;
		mutable std::map<char32_t, std::optional<FontReplacement::RasterGlyph>> m_glyphs;

	public:
		ElementBaseFont(FontReplacement::IElementFont& font, float px)
			: m_font(font)
			, m_px(px)
			, m_metrics(font.GetLineMetrics(px, nullptr)) {}

		std::string family_name() const override { return {}; }
		std::string subfamily_name() const override { return {}; }
		float font_size() const override { return m_px; }
		int ascent() const override { return m_metrics.Ascent; }
		int line_height() const override { return m_metrics.LineHeight; }

		const std::set<char32_t>& all_codepoints() const override {
			static const std::set<char32_t> s_none;
			return s_none;
		}

		bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override {
			const auto& g = Get(codepoint);
			if (!g)
				return false;
			gm = {g->Left, g->Top + m_metrics.Ascent, g->Left + g->Width, g->Top + m_metrics.Ascent + g->Height, g->Advance};
			return true;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override {
			static const std::map<std::pair<char32_t, char32_t>, int> s_none;
			return s_none;
		}

		bool draw(char32_t, xivres::util::b8g8r8a8*, int, int, int, int, xivres::util::b8g8r8a8, xivres::util::b8g8r8a8) const override {
			return false;
		}

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t, uint8_t, uint8_t, uint8_t) const override {
			const auto& g = Get(codepoint);
			if (!g)
				return false;
			BlitInto(pBuf, stride, destWidth, destHeight, *g, drawX + g->Left, drawY + m_metrics.Ascent + g->Top);
			return true;
		}

		std::shared_ptr<fixed_size_font> get_threadsafe_view() const override { return std::make_shared<ElementBaseFont>(m_font, m_px); }
		const fixed_size_font* get_base_font(char32_t) const override { return this; }
		std::optional<float> get_baseline(uint32_t baselineTag) const override { return m_font.GetBaseline(baselineTag, m_px); }

	private:
		const std::optional<FontReplacement::RasterGlyph>& Get(char32_t codepoint) const {
			auto it = m_glyphs.find(codepoint);
			if (it == m_glyphs.end())
				it = m_glyphs.emplace(codepoint, m_font.Rasterize(codepoint, m_px)).first;
			return it->second;
		}
	};

	// Texts of glyph merging in a font, shaped with its features, measured by its glyphs' metrics.
	class OutlineTextFont final : public default_abstract_fixed_size_font {
		FontReplacement::OutlineElementFont& m_font;
		FontReplacement::GlyphRasterizer& m_rasterizer;
		float m_size;
		FontReplacement::GlyphTransform m_transform;
		FontReplacement::LineMetrics m_metrics;

		// The glyphs of the line being shaped, and their advances and offsets (shaped from the const fixed_size_font calls).
		struct ShapedGlyph {
			uint16_t Index;
			float Advance;
			DWRITE_GLYPH_OFFSET Offset;
		};
		mutable std::vector<ShapedGlyph> m_shaped;

	public:
		OutlineTextFont(FontReplacement::OutlineElementFont& font, FontReplacement::GlyphRasterizer& rasterizer, float size, const FontReplacement::GlyphTransform& transform)
			: m_font(font)
			, m_rasterizer(rasterizer)
			, m_size(size)
			, m_transform(transform)
			, m_metrics(font.GetLineMetrics(size, transform)) {}

		std::string family_name() const override { return m_font.Lookup().Name; }
		std::string subfamily_name() const override { return {}; }
		float font_size() const override { return m_size; }
		int ascent() const override { return m_metrics.Ascent; }
		int line_height() const override { return m_metrics.LineHeight; }
		const std::set<char32_t>& all_codepoints() const override { return m_font.AllCodepoints(); }

		bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override {
			uint16_t index;
			float advance;
			if (!GetGlyph(codepoint, index, advance))
				return false;
			if (!try_get_glyph_index_metrics(index, 0, static_cast<float>(m_metrics.Ascent), gm))
				return false;
			gm.AdvanceX = static_cast<int>(std::round(advance * m_transform.M11));
			return true;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& all_kerning_pairs() const override {
			static const std::map<std::pair<char32_t, char32_t>, int> s_none;
			return s_none;
		}

		bool draw(char32_t, xivres::util::b8g8r8a8*, int, int, int, int, xivres::util::b8g8r8a8, xivres::util::b8g8r8a8) const override {
			return false;
		}

		bool draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			uint16_t index;
			float advance;
			if (!GetGlyph(codepoint, index, advance))
				return false;
			return draw_glyph_index(index, pBuf, stride, static_cast<float>(drawX), static_cast<float>(drawY + m_metrics.Ascent), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
		}

		std::shared_ptr<fixed_size_font> get_threadsafe_view() const override {
			return std::make_shared<OutlineTextFont>(m_font, m_rasterizer, m_size, m_transform);
		}

		const fixed_size_font* get_base_font(char32_t) const override { return this; }

		// Shaped as the plugin's OutlineTextFont lays texts out: in the font only, with its features; letter spacing goes
		// between glyphs.
		std::optional<shaped_line> shape_line(std::u32string_view text, int letterSpacing) const override {
			try {
				m_shaped.clear();
				if (!text.empty())
					Shape(xivres::util::unicode::convert<std::wstring>(text));

				const auto extra = m_font.GetExtraAdvance(m_size);
				shaped_line res;
				auto along = 0.f;
				for (size_t i = 0; i < m_shaped.size(); i++) {
					const auto& g = m_shaped[i];
					const auto x = along + g.Offset.advanceOffset;
					const auto y = -g.Offset.ascenderOffset;
					res.Glyphs.push_back({
						.GlyphIndex = g.Index,
						.X = m_transform.M11 * x + m_transform.M12 * y + static_cast<float>(static_cast<int>(i) * letterSpacing),
						.Y = m_transform.M21 * x + m_transform.M22 * y,
					});
					along += g.Advance + extra;
				}
				res.AdvanceWidth = static_cast<int>(std::round(along * m_transform.M11))
					+ (m_shaped.empty() ? 0 : static_cast<int>(m_shaped.size() - 1) * letterSpacing);
				return res;
			} catch (const std::exception& e) {
				FontReplacement::Host::Error("Shaping a merged glyph's text failed: {}", e.what());
				return std::nullopt;
			}
		}

		bool try_get_glyph_index_metrics(uint32_t glyphIndex, float originX, float originY, glyph_metrics& gm) const override {
			const auto face = m_font.Font().GetFace(m_size);
			if (!glyphIndex || glyphIndex >= face->GetGlyphCount())
				return false;
			const auto index = static_cast<uint16_t>(glyphIndex);
			const auto g = m_font.RasterizeRun(face, m_size, &index, &s_zero, nullptr, 1, originX, 0, 1, &m_transform, originY);
			gm = {g.Left, g.Top, g.Left + g.Width, g.Top + g.Height, 0};
			return true;
		}

		bool try_get_glyph_index_ink_extent(uint32_t glyphIndex, float& x1, float& x2) const override {
			const auto face = m_font.Font().GetFace(m_size);
			if (!glyphIndex || glyphIndex >= face->GetGlyphCount())
				return false;
			std::tie(x1, x2) = FontReplacement::OutlineElementFont::GetInkExtent(face, m_size, static_cast<uint16_t>(glyphIndex), m_transform);
			return true;
		}

		bool draw_glyph_index(uint32_t glyphIndex, uint8_t* pBuf, size_t stride, float drawX, float drawY, int destWidth, int destHeight, uint8_t, uint8_t, uint8_t, uint8_t) const override {
			const auto face = m_font.Font().GetFace(m_size);
			if (!glyphIndex || glyphIndex >= face->GetGlyphCount())
				return false;
			const auto index = static_cast<uint16_t>(glyphIndex);
			const auto g = m_font.RasterizeRun(face, m_size, &index, &s_zero, nullptr, 1, drawX, 0, 1, &m_transform, drawY);
			BlitInto(pBuf, stride, destWidth, destHeight, g, g.Left, g.Top);
			return true;
		}

	private:
		static constexpr float s_zero = 0;

		bool GetGlyph(char32_t codepoint, uint16_t& index, float& advance) const {
			const auto face = m_font.Font().GetFace(m_size);
			const auto cp = static_cast<UINT32>(codepoint);
			if (FAILED(face->GetGlyphIndices(&cp, 1, &index)) || !index)
				return false;
			DWRITE_GLYPH_METRICS gm;
			if (FAILED(face->GetDesignGlyphMetrics(&index, 1, &gm, FALSE)))
				return false;
			advance = static_cast<float>(gm.advanceWidth) * m_size / static_cast<float>(m_font.Font().Metrics().designUnitsPerEm) + m_font.GetExtraAdvance(m_size);
			return true;
		}

		void OnGlyphRun(const DWRITE_GLYPH_RUN* run) const {
			// Glyphs the font lacks are left out.
			for (UINT32 i = 0; i < run->glyphCount; i++) {
				if (run->glyphIndices[i] == 0) {
					if (!m_shaped.empty())
						m_shaped.back().Advance += run->glyphAdvances[i];
					continue;
				}
				m_shaped.push_back({run->glyphIndices[i], run->glyphAdvances[i], run->glyphOffsets ? run->glyphOffsets[i] : DWRITE_GLYPH_OFFSET{}});
			}
		}

		void Shape(const std::wstring& text) const {
			const auto& font = m_font.Font();
			const auto format = m_rasterizer.CreateFormat(m_font.Lookup().Name, font.LayoutWeight(), font.LayoutStyle(), font.LayoutStretch(), m_size, m_rasterizer.NoFallback());
			IDWriteTextLayoutPtr layout;
			ThrowOnError(m_rasterizer.Factory()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, 1e6f, 1e6f, &layout), "CreateTextLayout");
			m_font.ApplyTo(layout, {0, static_cast<UINT32>(text.size())}, m_size);
			m_rasterizer.RunCollector().Collect(layout, [this](float, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*) { OnGlyphRun(run); });
		}
	};
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::RasterizeFixedSizeFontGlyph(const fixed_size_font& font, char32_t codepoint) {
	glyph_metrics gm;
	if (!font.try_get_glyph_metrics(codepoint, gm))
		return std::nullopt;
	RasterGlyph g{gm.AdvanceX, gm.X1, gm.Y1 - font.ascent(), (std::max)(0, gm.width()), (std::max)(0, gm.height()), {}};
	if (g.Width == 0 || g.Height == 0) {
		g.Width = g.Height = 0;
		return g;
	}
	g.Alpha.resize(static_cast<size_t>(g.Width) * g.Height);
	font.draw(codepoint, g.Alpha.data(), 1, -gm.X1, -gm.Y1, g.Width, g.Height, 255, 0, 255, 0);
	return g;
}

bool FontReplacement::GameElementFont::Has(char32_t codepoint, GameFont* game) {
	return game && game->HasGlyph(codepoint);
}

FontReplacement::LineMetrics FontReplacement::GameElementFont::GetLineMetrics(float px, GameFont* game) {
	if (!game || game->Size() <= 0)
		return {};
	const auto scale = px / game->Size();
	return {static_cast<int>(std::round(static_cast<float>(game->Ascent()) * scale)), static_cast<int>(std::round(static_cast<float>(game->LineHeight()) * scale))};
}

FontReplacement::LineMetrics FontReplacement::EmptyElementFont::GetLineMetrics(float px, GameFont* game) {
	if (m_size <= 0)
		return {};
	return {static_cast<int>(std::round(m_ascent * px / m_size)), static_cast<int>(std::round(m_lineHeight * px / m_size))};
}

FontReplacement::OutlineElementFont::OutlineElementFont(GlyphRasterizer& rasterizer, const FontChanger::Structs::FaceElement& def, std::unique_ptr<LookupFont> font, FT_FaceRec_* freeTypeFace, std::optional<std::vector<uint8_t>> gamma)
	: m_rasterizer(rasterizer)
	, m_def(def)
	, m_font(std::move(font))
	, m_freeTypeFace(freeTypeFace)
	, m_gamma(std::move(gamma))
	, m_transform(GlyphTransform::Of(def.Transform).After(m_font->Transform())) {
}

std::unique_ptr<FontReplacement::OutlineElementFont> FontReplacement::OutlineElementFont::Create(GlyphRasterizer& rasterizer, const FontChanger::Structs::FaceElement& def, RendererEnum renderer, std::optional<std::vector<uint8_t>> gamma, const std::string& faceName) {
	auto font = LookupFont::Create(rasterizer, def.Lookup, renderer);
	if (!font) {
		Host::Warning("{}: font {} not found", faceName, def.Lookup.Name);
		return nullptr;
	}

	// FreeType draws what the preset says it does, and fonts with only bitmaps, which DirectWrite doesn't draw.
	auto ftFace = rasterizer.FreeType() ? rasterizer.FreeType()->Open(font->Face()) : nullptr;
	if (!ftFace || (renderer != RendererEnum::FreeType && FreeTypeFonts::IsScalable(ftFace))) {
		if (renderer == RendererEnum::FreeType)
			Host::Warning("{} can't be opened with FreeType; it is drawn with DirectWrite", def.Lookup.Name);
		ftFace = nullptr;
	}

	return std::unique_ptr<OutlineElementFont>(new OutlineElementFont(rasterizer, def, std::move(font), ftFace, std::move(gamma)));
}

const std::set<char32_t>& FontReplacement::OutlineElementFont::AllCodepoints() {
	if (!m_codepoints) {
		m_codepoints.emplace();
		if (IDWriteFontFace1Ptr face1; SUCCEEDED(m_font->Face()->QueryInterface(__uuidof(IDWriteFontFace1), reinterpret_cast<void**>(&face1)))) {
			UINT32 count = 0;
			face1->GetUnicodeRanges(0, nullptr, &count);
			std::vector<DWRITE_UNICODE_RANGE> ranges(count);
			if (SUCCEEDED(face1->GetUnicodeRanges(count, ranges.data(), &count))) {
				for (const auto& r : ranges) {
					for (auto c = r.first; c <= r.last; c++)
						m_codepoints->insert(static_cast<char32_t>(c));
				}
			}
		}
	}
	return *m_codepoints;
}

bool FontReplacement::OutlineElementFont::Has(char32_t codepoint, GameFont* game) {
	BOOL exists;
	return SUCCEEDED(m_font->Font()->HasCharacter(static_cast<UINT32>(codepoint), &exists)) && exists;
}

FontReplacement::LineMetrics FontReplacement::OutlineElementFont::GetLineMetrics(float px, const GlyphTransform& transform) const {
	const auto& m = m_font->Metrics();
	const auto size = px / static_cast<float>(m.designUnitsPerEm) * std::abs(transform.M22);
	const auto ascent = static_cast<int>(std::ceil(static_cast<float>(m.ascent) * size));
	const auto descent = static_cast<int>(std::ceil(static_cast<float>(m.descent) * size));
	return {ascent, ascent + descent + static_cast<int>(std::round(static_cast<float>(m.lineGap) * size))};
}

std::optional<float> FontReplacement::OutlineElementFont::GetBaseline(uint32_t tag, float px) {
	if (!m_baselines)
		m_baselines = ReadBaselines(m_font->Face());
	const auto it = m_baselines->find(tag);
	if (it == m_baselines->end())
		return std::nullopt;
	return static_cast<float>(it->second) * px * m_transform.M22 / static_cast<float>(m_font->Metrics().designUnitsPerEm);
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::OutlineElementFont::Rasterize(char32_t codepoint, float px) {
	IDWriteFontFace* face;
	uint16_t index;
	float advance;
	if (!TryGetGlyph(codepoint, px, face, index, advance))
		return std::nullopt;
	return RasterizeRun(face, px, &index, &advance, nullptr, 1, 0, static_cast<int>(std::round(advance * m_transform.M11)));
}

FontReplacement::RasterGlyph FontReplacement::OutlineElementFont::Squeeze(char32_t codepoint, float px, const RasterGlyph& glyph, float scale) {
	IDWriteFontFace* face;
	uint16_t index;
	float advance;
	if (!TryGetGlyph(codepoint, px, face, index, advance))
		return glyph;
	return RasterizeRun(face, px, &index, &advance, nullptr, 1, 0, glyph.Advance, scale);
}

float FontReplacement::OutlineElementFont::GetAdvance(char32_t codepoint, float px) {
	IDWriteFontFace* face;
	uint16_t index;
	float advance;
	return TryGetGlyph(codepoint, px, face, index, advance) ? advance * m_transform.M11 : 0.f;
}

float FontReplacement::OutlineElementFont::GetExtraAdvance(float emSize) const {
	return m_freeTypeFace ? m_font->Embolden() * emSize : 0.f;
}

FontReplacement::RasterGlyph FontReplacement::OutlineElementFont::RasterizeRun(
	IDWriteFontFace* face,
	float size,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	float squeezeX,
	const GlyphTransform* transform,
	float originY) {
	const auto t = (transform ? *transform : m_transform).ScaledX(squeezeX);

	// FreeType draws the font's own face; another face's glyphs (its family's italic) are DirectWrite's.
	RasterGlyph glyph;
	if (m_freeTypeFace && SameFont(m_font->GetFace(size), face)) {
		const auto axes = m_font->GetAxisValues(size);
		glyph = m_rasterizer.FreeType()->RasterizeRun(
			m_freeTypeFace, size, glyphs, advances, offsets, count, originX, advance, FreeTypeParams::Of(m_def.RendererSpecific.FreeType), t, &axes, m_font->Embolden(), originY);
	} else {
		auto parameters = DirectWriteParams::Of(m_def.RendererSpecific.DirectWrite);
		parameters.MeasureMode = MeasureMode;
		glyph = m_rasterizer.RasterizeRun(face, size, glyphs, advances, offsets, count, originX, advance, parameters, t, originY);
	}
	return m_gamma ? glyph.WithCoverage(*m_gamma) : glyph;
}

std::optional<std::pair<int, int>> FontReplacement::OutlineElementFont::MeasureGlyph(IDWriteFontFace* face, float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const GlyphTransform& transform) {
	const auto advance = 0.f;
	RECT bounds;
	if (m_freeTypeFace && SameFont(m_font->GetFace(size), face)) {
		const auto axes = m_font->GetAxisValues(size);
		const auto g = m_rasterizer.FreeType()->RasterizeRun(m_freeTypeFace, size, &glyph, &advance, &offset, 1, 0, 0, FreeTypeParams::Of(m_def.RendererSpecific.FreeType), transform, &axes, m_font->Embolden());
		bounds = {g.Left, g.Top, g.Left + g.Width, g.Top + g.Height};
	} else {
		auto parameters = DirectWriteParams::Of(m_def.RendererSpecific.DirectWrite);
		parameters.MeasureMode = MeasureMode;
		bounds = m_rasterizer.GetRunBounds(face, size, &glyph, &advance, &offset, 1, parameters, transform);
	}
	if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
		return std::nullopt;
	return std::make_pair(static_cast<int>(bounds.top), static_cast<int>(bounds.bottom));
}

std::pair<float, float> FontReplacement::OutlineElementFont::GetInkExtent(IDWriteFontFace* face, float size, uint16_t glyph, const GlyphTransform& transform) {
	DWRITE_GLYPH_METRICS gm;
	if (FAILED(face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)))
		return {0.f, 0.f};
	DWRITE_FONT_METRICS m;
	face->GetMetrics(&m);
	const auto scale = size / static_cast<float>(m.designUnitsPerEm) * transform.M11;
	const auto x1 = static_cast<float>(gm.leftSideBearing) * scale;
	const auto x2 = (static_cast<float>(gm.advanceWidth) - static_cast<float>(gm.rightSideBearing)) * scale;
	return x1 <= x2 ? std::make_pair(x1, x2) : std::make_pair(x2, x1);
}

void FontReplacement::OutlineElementFont::ApplyTo(IDWriteTextLayout* layout, DWRITE_TEXT_RANGE range, float emSize) {
	const auto& lookup = m_def.Lookup;
	const auto& font = *m_font;
	ThrowOnError(layout->SetFontFamilyName(xivres::util::unicode::convert<std::wstring>(lookup.Name).c_str(), range), "SetFontFamilyName");
	ThrowOnError(layout->SetFontWeight(font.LayoutWeight(), range), "SetFontWeight");
	ThrowOnError(layout->SetFontStretch(font.LayoutStretch(), range), "SetFontStretch");
	ThrowOnError(layout->SetFontStyle(font.LayoutStyle(), range), "SetFontStyle");
	ThrowOnError(layout->SetFontSize(emSize, range), "SetFontSize");
	if (!lookup.Language.empty())
		ThrowOnError(layout->SetLocaleName(xivres::util::unicode::convert<std::wstring>(lookup.Language).c_str(), range), "SetLocaleName");

	if (const auto typography = GetTypography())
		ThrowOnError(layout->SetTypography(typography, range), "SetTypography");

	// A variable font's axes; the optical size follows the size unless set.
	if (IDWriteTextLayout4Ptr layout4; SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout4), reinterpret_cast<void**>(&layout4)))) {
		const auto& axes = font.LayoutAxes();
		if (!axes.empty())
			ThrowOnError(layout4->SetFontAxisValues(axes.data(), static_cast<UINT32>(axes.size()), range), "SetFontAxisValues");
		if (font.AutoOpticalSize())
			ThrowOnError(layout4->SetAutomaticFontAxes(DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE), "SetAutomaticFontAxes");
	}
}

IDWriteFontFace* FontReplacement::OutlineElementFont::GetRunFace(IDWriteFontFace* runFace, float emSize) {
	const auto own = m_font->GetFace(emSize);
	return SameFont(own, runFace) ? own : runFace;
}

bool FontReplacement::OutlineElementFont::TryGetGlyph(char32_t codepoint, float px, IDWriteFontFace*& face, uint16_t& index, float& advance) {
	face = m_font->GetFace(px);
	const auto cp = static_cast<UINT32>(codepoint);
	advance = 0;
	if (FAILED(face->GetGlyphIndices(&cp, 1, &index)) || !index) {
		index = 0;
		return false;
	}

	DWRITE_GLYPH_METRICS gm;
	ThrowOnError(face->GetDesignGlyphMetrics(&index, 1, &gm, FALSE), "GetDesignGlyphMetrics");
	advance = static_cast<float>(gm.advanceWidth) * px / static_cast<float>(m_font->Metrics().designUnitsPerEm) + GetExtraAdvance(px);
	return true;
}

IDWriteTypography* FontReplacement::OutlineElementFont::GetTypography() {
	if (m_typographyMade)
		return m_typography;

	m_typographyMade = true;
	const auto& features = m_def.Lookup.Features;
	if (features.empty())
		return nullptr;
	ThrowOnError(m_rasterizer.Factory()->CreateTypography(&m_typography), "CreateTypography");
	for (const auto& [tag, value] : features)
		ThrowOnError(m_typography->AddFontFeature({tag, value}), "AddFontFeature");
	return m_typography;
}

bool FontReplacement::OutlineElementFont::SameFont(IDWriteFontFace* a, IDWriteFontFace* b) {
	if (a == b)
		return true;
	if (const auto it = m_sameFonts.find({a, b}); it != m_sameFonts.end())
		return it->second.Same;

	auto same = false;
	if (a->GetIndex() == b->GetIndex() && a->GetGlyphCount() == b->GetGlyphCount())
		same = FontFileKey::Of(a).SameFileAs(FontFileKey::Of(b));

	m_sameFonts.emplace(std::make_pair(a, b), SameFontEntry{a, b, same});
	return same;
}

FontReplacement::ImageElementFont::ImageElementFont(FontChanger::Structs::GlyphImagesStruct settings, float gamma, const GlyphTransform& transform)
	: m_settings(std::move(settings))
	, m_gamma(gamma)
	, m_transform(transform) {
	for (const auto& codepoint : FontChanger::GlyphFiles::LoadGlyphSet(m_settings)->Glyphs | std::views::keys)
		m_codepoints.insert(codepoint);
}

const std::shared_ptr<fixed_size_font>& FontReplacement::ImageElementFont::GetFont(float px, const GlyphTransform& transform) {
	const auto key = std::make_tuple(px, transform.M11, transform.M12, transform.M21, transform.M22);
	auto it = m_fonts.find(key);
	if (it == m_fonts.end())
		it = m_fonts.emplace(key, FontChanger::GlyphFiles::CreateFont(m_settings, px, m_gamma, transform.ToMatrix())).first;
	return it->second;
}

FontReplacement::LineMetrics FontReplacement::ImageElementFont::GetLineMetrics(float px, GameFont* game) {
	const auto& font = GetFont(px, m_transform);
	return {font->ascent(), font->line_height()};
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::ImageElementFont::Rasterize(char32_t codepoint, float px) {
	return RasterizeFixedSizeFontGlyph(*GetFont(px, m_transform), codepoint);
}

float FontReplacement::ImageElementFont::GetAdvance(char32_t codepoint, float px) {
	const auto g = Rasterize(codepoint, px);
	return g ? static_cast<float>(g->Advance) : 0.f;
}

FontReplacement::MergingElementFont::MergingElementFont(GlyphRasterizer& rasterizer, std::unique_ptr<IElementFont> baseFont, const FontChanger::Structs::FaceElement& def, std::unique_ptr<OutlineElementFont> ownTextOutline)
	: m_rasterizer(rasterizer)
	, m_baseFont(std::move(baseFont))
	, m_def(def)
	, m_ownTextOutline(std::move(ownTextOutline))
	, m_textOutline(m_ownTextOutline ? m_ownTextOutline.get() : dynamic_cast<OutlineElementFont*>(m_baseFont.get())) {
	for (const auto& mapping : def.GlyphMerging.Params.Mappings) {
		for (size_t i = 0; i < mapping.Codepoints.size() && i < mapping.Texts.size(); i++) {
			if (!mapping.Texts[i].empty())
				m_codepoints.insert(mapping.Codepoints[i]);
		}
	}
}

std::vector<FontReplacement::OutlineElementFont*> FontReplacement::MergingElementFont::OutlineFonts() const {
	std::vector<OutlineElementFont*> res;
	if (const auto outline = dynamic_cast<OutlineElementFont*>(m_baseFont.get()))
		res.push_back(outline);
	if (m_ownTextOutline)
		res.push_back(m_ownTextOutline.get());
	return res;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::MergingElementFont::Rasterize(char32_t codepoint, float px) {
	if (!m_codepoints.contains(codepoint))
		return std::nullopt;

	auto it = m_fonts.find(px);
	if (it == m_fonts.end()) {
		// Pixel values of the merging are at the face's size, which the element's is to its own.
		const auto pixelScale = m_def.Size > 0 ? px / m_def.Size : 1.f;
		auto params = m_def.GlyphMerging.Params;
		if (params.TextSize)
			*params.TextSize *= pixelScale;
		params.TextOffsetX *= pixelScale;
		params.TextOffsetY *= pixelScale;
		params.LetterSpacing *= pixelScale;
		params.LineSpacing *= pixelScale;

		// The element's transformation, then the texts', then the condensing; texts are drawn with the element's font, or for
		// glyph images, with the font the lookup names if any, else with their own files.
		const auto elementTransform = GlyphTransform::Of(m_def.Transform);
		const auto textTransform = GlyphTransform::Of(m_def.GlyphMerging.TextTransform);
		const auto textOutline = m_textOutline;
		const auto images = dynamic_cast<ImageElementFont*>(m_baseFont.get());
		auto textFont = [&rasterizer = m_rasterizer, textOutline, images, elementTransform, textTransform](float size, float condense) -> std::shared_ptr<fixed_size_font> {
			const auto transform = GlyphTransform{condense, 0, 0, 1}.After(textTransform.After(elementTransform));
			if (textOutline)
				return std::make_shared<OutlineTextFont>(*textOutline, rasterizer, size, transform.After(textOutline->Font().Transform()));
			if (images)
				return images->GetFont(size, transform);
			return nullptr;
		};
		it = m_fonts.emplace(px, std::make_shared<glyph_merging_fixed_size_font>(std::make_shared<ElementBaseFont>(*m_baseFont, px), std::move(params), std::move(textFont))).first;
	}

	return RasterizeFixedSizeFontGlyph(*it->second, codepoint);
}
