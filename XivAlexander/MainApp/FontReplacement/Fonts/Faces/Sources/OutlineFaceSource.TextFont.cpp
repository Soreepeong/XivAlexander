#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

#include <FontChanger.FixedSizeFont/fixed_size_font.h>

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/Records/TextRun.h"
#include "MainApp/FontReplacement/Fonts/Text/DirectWriteTextAnalyzer.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using namespace FontChanger::FixedSizeFont;

namespace {
	// Into a fixed_size_font::draw buffer, keeping the larger value; (x, y) is where the coverage's top left goes.
	void BlitInto(uint8_t* buffer, size_t stride, int destWidth, int destHeight, const FontReplacement::RasterGlyph& g, int x, int y) {
		for (auto row = (std::max)(0, -y); row < g.Height && row + y < destHeight; row++) {
			for (auto col = (std::max)(0, -x); col < g.Width && col + x < destWidth; col++) {
				auto& d = buffer[(static_cast<size_t>(row + y) * destWidth + col + x) * stride];
				d = (std::max)(d, g.Alpha[static_cast<size_t>(row) * g.Width + col]);
			}
		}
	}
}

// Glyph merging's texts drawn with the font: shaped as the source shapes (with letter spacing), measured by the glyphs' outlines, drawn as the
// source draws.
class FontReplacement::OutlineFaceSource::TextFont final : public default_abstract_fixed_size_font {
	OutlineFaceSource& m_font;
	float m_size;
	Matrix2x2 m_transform;
	LineMetrics m_metrics;
	mutable std::vector<TextRun> m_runs;
	mutable std::vector<ShapedCluster> m_clusters;

public:
	TextFont(OutlineFaceSource& font, float size, const Matrix2x2& transform)
		: m_font(font)
		, m_size(size)
		, m_transform(transform)
		, m_metrics(font.GetLineMetrics(size, transform)) {}

	std::string family_name() const override { return m_font.m_def.Lookup.FamilyName; }
	std::string subfamily_name() const override { return {}; }
	float font_size() const override { return m_size; }
	int ascent() const override { return m_metrics.Ascent; }
	int line_height() const override { return m_metrics.LineHeight; }
	const std::set<char32_t>& all_codepoints() const override { return m_font.Codepoints(); }

	bool try_get_glyph_metrics(char32_t codepoint, glyph_metrics& gm) const override {
		const auto index = m_font.GetGlyphId(codepoint);
		if (!index || !try_get_glyph_index_metrics(index, 0, static_cast<float>(m_metrics.Ascent), gm))
			return false;
		gm.AdvanceX = static_cast<int>(std::round((m_font.GetAdvance(m_size, index) + m_font.GetExtraAdvance(m_size)) * m_transform.M11));
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
		const auto index = m_font.GetGlyphId(codepoint);
		if (!index)
			return false;
		return draw_glyph_index(index, pBuf, stride, static_cast<float>(drawX), static_cast<float>(drawY + m_metrics.Ascent), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
	}

	std::shared_ptr<fixed_size_font> get_threadsafe_view() const override {
		return std::make_shared<TextFont>(m_font, m_size, m_transform);
	}

	const fixed_size_font* get_base_font(char32_t) const override { return this; }

	// Each cluster advances letterSpacing further (and as emboldening does); glyphs the font lacks are left out.
	std::optional<shaped_line> shape_line(std::u32string_view text, int letterSpacing) const override {
		try {
			const auto spacing = m_transform.M11 != 0 ? static_cast<float>(letterSpacing) / m_transform.M11 : 0.f;
			const auto extra = m_font.GetExtraAdvance(m_size);
			const auto wide = xivres::util::unicode::convert<std::wstring>(text);

			struct Glyph {
				uint32_t Index;
				float Advance;
				GlyphOffset Offset;
			};
			std::vector<Glyph> glyphs;
			m_font.m_services.TextAnalyzer().Itemize(wide, m_runs);
			for (const auto& run : m_runs) {
				for (auto start = run.Start; start < run.End;) {
					m_clusters.clear();
					start += m_font.Shape(m_size, std::wstring_view(wide).substr(start, run.End - start), run.Script, m_clusters);
					for (const auto& c : m_clusters) {
						for (size_t i = 0; i < c.GlyphIds.size(); i++)
							glyphs.push_back({c.GlyphIds[i], c.Advances[i] + extra, i < c.Offsets.size() ? c.Offsets[i] : GlyphOffset{}});
						if (!glyphs.empty())
							glyphs.back().Advance += spacing;
					}

					// Shaping stops at a glyph the font lacks.
					if (start < run.End)
						start += start + 1 < run.End && IS_SURROGATE_PAIR(wide[start], wide[start + 1]) ? 2 : 1;
				}
			}

			shaped_line res;
			auto along = 0.f;
			for (const auto& g : glyphs) {
				const auto x = along + g.Offset.Advance;
				const auto y = -g.Offset.Ascender;
				res.Glyphs.push_back({
					.GlyphIndex = g.Index,
					.X = m_transform.M11 * x + m_transform.M12 * y,
					.Y = m_transform.M21 * x + m_transform.M22 * y,
				});
				along += g.Advance;
			}
			res.AdvanceWidth = glyphs.empty() ? 0 : static_cast<int>(std::round((along - spacing) * m_transform.M11));
			return res;
		} catch (const std::exception& e) {
			Host::Error("Shaping a merged glyph's text failed: {}", e.what());
			return std::nullopt;
		}
	}

	bool try_get_glyph_index_metrics(uint32_t glyphIndex, float originX, float originY, glyph_metrics& gm) const override {
		if (!glyphIndex || glyphIndex >= m_font.m_font->GetFace(m_size)->GetGlyphCount())
			return false;
		const auto index = static_cast<uint16_t>(glyphIndex);
		const auto g = m_font.RasterizeRun(m_size, &index, &s_zero, nullptr, 1, originX, 0, m_transform, originY);
		gm = {g.Left, g.Top, g.Left + g.Width, g.Top + g.Height, 0};
		return true;
	}

	bool try_get_glyph_index_ink_extent(uint32_t glyphIndex, float& x1, float& x2) const override {
		if (!glyphIndex || glyphIndex >= m_font.m_font->GetFace(m_size)->GetGlyphCount())
			return false;
		std::tie(x1, x2) = m_font.GetInkExtent(m_size, static_cast<uint16_t>(glyphIndex), m_transform);
		return true;
	}

	bool draw_glyph_index(uint32_t glyphIndex, uint8_t* pBuf, size_t stride, float drawX, float drawY, int destWidth, int destHeight, uint8_t, uint8_t, uint8_t, uint8_t) const override {
		if (!glyphIndex || glyphIndex >= m_font.m_font->GetFace(m_size)->GetGlyphCount())
			return false;
		const auto index = static_cast<uint16_t>(glyphIndex);
		const auto g = m_font.RasterizeRun(m_size, &index, &s_zero, nullptr, 1, drawX, 0, m_transform, drawY);
		BlitInto(pBuf, stride, destWidth, destHeight, g, g.Left, g.Top);
		return true;
	}

private:
	static constexpr float s_zero = 0;
};

std::shared_ptr<fixed_size_font> FontReplacement::OutlineFaceSource::CreateMergeTextFont(float size, const Matrix2x2& transform) {
	return std::make_shared<TextFont>(*this, size, transform * m_font->Transform());
}

std::pair<float, float> FontReplacement::OutlineFaceSource::GetInkExtent(float size, uint16_t glyph, const Matrix2x2& transform) {
	DWRITE_GLYPH_METRICS gm;
	if (FAILED(m_font->GetFace(size)->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)))
		return {0.f, 0.f};
	const auto scale = size / static_cast<float>(m_font->Metrics().designUnitsPerEm) * transform.M11;
	const auto x1 = static_cast<float>(gm.leftSideBearing) * scale;
	const auto x2 = (static_cast<float>(gm.advanceWidth) - static_cast<float>(gm.rightSideBearing)) * scale;
	return x1 <= x2 ? std::make_pair(x1, x2) : std::make_pair(x2, x1);
}
