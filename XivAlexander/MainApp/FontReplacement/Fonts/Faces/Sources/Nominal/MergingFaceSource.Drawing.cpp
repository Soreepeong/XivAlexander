#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/MergingFaceSource.h"

#include <FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using namespace FontChanger::FixedSizeFont;

// Glyph merging's base font: the inner source's line metrics, baselines and own glyphs at a size.
class FontReplacement::MergingFaceSource::BaseFont final : public default_abstract_fixed_size_font {
	MergingFaceSource& m_source;
	float m_px;
	LineMetrics m_metrics;
	mutable std::map<char32_t, std::optional<RasterGlyph>> m_glyphs;

public:
	BaseFont(MergingFaceSource& source, float px)
		: m_source(source)
		, m_px(px)
		, m_metrics(source.m_inner->GetLineMetrics(px)) {}

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
		const auto x = drawX + g->Left;
		const auto y = drawY + m_metrics.Ascent + g->Top;
		for (auto row = (std::max)(0, -y); row < g->Height && row + y < destHeight; row++) {
			for (auto col = (std::max)(0, -x); col < g->Width && col + x < destWidth; col++) {
				auto& d = pBuf[(static_cast<size_t>(row + y) * destWidth + col + x) * stride];
				d = (std::max)(d, g->Alpha[static_cast<size_t>(row) * g->Width + col]);
			}
		}
		return true;
	}

	std::shared_ptr<fixed_size_font> get_threadsafe_view() const override { return std::make_shared<BaseFont>(m_source, m_px); }
	const fixed_size_font* get_base_font(char32_t) const override { return this; }

	std::optional<float> get_baseline(uint32_t baselineTag) const override {
		for (const auto baseline : {Baseline::Roman, Baseline::IdeographicBottom, Baseline::IdeographicTop, Baseline::IdeographicFaceBottom, Baseline::IdeographicFaceTop}) {
			if (OutlineFaceSource::GetBaselineTag(baseline) == baselineTag)
				return m_source.m_inner->TryGetBaseline(m_px, baseline);
		}
		return std::nullopt;
	}

private:
	const std::optional<RasterGlyph>& Get(char32_t codepoint) const {
		auto it = m_glyphs.find(codepoint);
		if (it == m_glyphs.end())
			it = m_glyphs.emplace(codepoint, m_source.DrawInner(codepoint, m_px)).first;
		return it->second;
	}
};

std::optional<FontReplacement::RasterGlyph> FontReplacement::MergingFaceSource::RasterizeGlyph(float px, uint32_t glyphId) {
	const auto codepoint = static_cast<char32_t>(glyphId);
	if (!m_codepoints.contains(codepoint))
		return std::nullopt;

	auto it = m_fonts.find(px);
	if (it == m_fonts.end()) {
		// Pixel values of the merging are at the font's size, which the element's is to its own.
		const auto pixelScale = m_def.Size > 0 ? px / m_def.Size : 1.f;
		auto params = m_def.GlyphMerging.Params;
		if (params.TextSize)
			*params.TextSize *= pixelScale;
		params.TextOffsetX *= pixelScale;
		params.TextOffsetY *= pixelScale;
		params.LetterSpacing *= pixelScale;
		params.LineSpacing *= pixelScale;

		it = m_fonts.emplace(px, std::make_shared<glyph_merging_fixed_size_font>(
			std::make_shared<BaseFont>(*this, px),
			std::move(params),
			[this](float size, float condense) { return CreateTextFont(size, condense); })).first;
	}

	return RasterizeFixedSizeFontGlyph(*it->second, codepoint);
}
