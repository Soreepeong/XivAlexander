#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"

#include <FontChanger.FixedSizeFont/fixed_size_font.h>

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

float FontReplacement::NominalFaceSource::GetAdvance(float px, uint32_t glyphId) {
	const auto key = std::make_pair(px, glyphId);
	if (const auto it = m_advances.find(key); it != m_advances.end())
		return it->second;

	auto glyph = RasterizeGlyph(px, glyphId);
	const auto advance = glyph ? static_cast<float>(glyph->Advance) : 0.f;
	m_measured.insert_or_assign(key, std::move(glyph));
	m_advances.emplace(key, advance);
	return advance;
}

int FontReplacement::NominalFaceSource::Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) {
	size_t position = 0;
	std::optional<size_t> last;
	while (position < text.size()) {
		const auto unit = static_cast<char32_t>(text[position]);
		char32_t codepoint;
		size_t length;
		if (unit >= 0xD800 && unit < 0xDC00) {
			if (position + 1 >= text.size() || text[position + 1] < 0xDC00 || text[position + 1] >= 0xE000)
				break;
			codepoint = 0x10000 + ((unit - 0xD800) << 10) + (static_cast<char32_t>(text[position + 1]) - 0xDC00);
			length = 2;
		} else if (unit >= 0xDC00 && unit < 0xE000) {
			break;
		} else {
			codepoint = unit;
			length = 1;
		}

		const auto id = GetGlyphId(codepoint);
		if (!id)
			break;

		if (last)
			clusters[*last].Advances[0] += GetKerning(px, clusters[*last].GlyphIds[0], id);

		last = clusters.size();
		clusters.push_back({
			.GlyphIds = {id},
			.Advances = {GetAdvance(px, id)},
			.Offsets = {},
			.TextStart = static_cast<int>(position),
			.TextEnd = static_cast<int>(position + length),
		});
		position += length;
	}

	return static_cast<int>(position);
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::NominalFaceSource::Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options) {
	std::optional<RasterGlyph> glyph;
	if (run.GlyphIds.size() == 1) {
		glyph = DrawGlyph(px, run.GlyphIds[0]);
	} else {
		std::vector<RasterGlyph> pieces;
		auto pen = 0.f;
		for (size_t i = 0; i < run.GlyphIds.size(); i++) {
			if (auto g = DrawGlyph(px, run.GlyphIds[i])) {
				const auto offset = i < run.Offsets.size() ? run.Offsets[i] : GlyphOffset{};
				g->Left += static_cast<int>(std::round(pen + offset.Advance));
				g->Top -= static_cast<int>(std::round(offset.Ascender));
				pieces.push_back(std::move(*g));
			}
			pen += run.Advances[i];
		}

		if (!pieces.empty())
			glyph = RasterGlyph::Merge(std::move(pieces), static_cast<int>(std::round(pen)));
	}

	if (!glyph)
		return std::nullopt;
	if (options.SqueezeX != 1)
		glyph = glyph->SqueezedX(options.SqueezeX);
	if (options.Advance)
		glyph->Advance = *options.Advance;
	return glyph;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::NominalFaceSource::RasterizeFixedSizeFontGlyph(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint) {
	FontChanger::FixedSizeFont::glyph_metrics gm;
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

std::optional<FontReplacement::RasterGlyph> FontReplacement::NominalFaceSource::DrawGlyph(float px, uint32_t glyphId) {
	if (const auto it = m_measured.find({px, glyphId}); it != m_measured.end()) {
		auto glyph = std::move(it->second);
		m_measured.erase(it);
		return glyph;
	}
	return RasterizeGlyph(px, glyphId);
}
