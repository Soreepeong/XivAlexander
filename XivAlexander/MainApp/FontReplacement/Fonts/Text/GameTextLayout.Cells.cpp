#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	int Share(int advance, int parts, int part) {
		return advance * (part + 1) / parts - advance * part / parts;
	}
}

void FontReplacement::GameTextLayout::PlaceClusters(const uint8_t* bytes, std::vector<GameGlyph*>& glyphs) {
	const auto count = m_clusters.size();
	std::vector<int> starts(count);
	for (size_t i = 0; i < count; i++) {
		auto& c = m_clusters[i];
		starts[i] = c.X0;
		if (c.Missing())
			continue;

		// A cluster after one left to the per-character path can't move into it.
		const auto& raster = GetRaster(c);
		if (i > 0 && !m_clusters[i - 1].Missing() && raster.Width > 0 && raster.Left < 0)
			starts[i] = (std::max)(c.X0 + raster.Left, starts[i - 1]);
	}

	for (size_t i = 0; i < count; i++) {
		auto& c = m_clusters[i];

		// A ligature's text elements share its advance evenly, so that the caret and selection can stop inside it.
		m_elementStarts.clear();
		for (auto t = c.TextStart; t < c.TextEnd; t += NextGrapheme(t, c.TextEnd))
			m_elementStarts.push_back(t);
		const auto elements = static_cast<int>(m_elementStarts.size());

		GameGlyph* cell = nullptr;
		auto advance = 0;
		if (!c.Missing()) {
			const auto end = i + 1 < count && !m_clusters[i + 1].Missing() ? starts[i + 1] : c.X1;
			advance = end - starts[i];
			cell = GetCell(c, bytes, c.X0 - starts[i], Share(advance, elements, 0));
		}

		auto element = 0;
		for (auto t = c.TextStart; t < c.TextEnd; t++) {
			const auto startsElement = element + 1 < elements && m_elementStarts[element + 1] == t;
			if (startsElement)
				element++;

			const auto b = m_textToByte[t];
			if (b < 0)
				continue;
			glyphs[b] = !cell ? nullptr
				: t == c.TextStart ? cell
				: startsElement ? GetSpacer(Share(advance, elements, element))
				: m_emptyGlyph;
		}
	}
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::GetSpacer(int advance) {
	const auto& sized = *m_shapingSized;
	const auto key = std::make_pair(&sized, advance);
	if (const auto it = m_spacers.find(key); it != m_spacers.end())
		return it->second;
	const auto cell = m_replacer.PlaceCell(sized, RasterGlyph{advance}, 0);
	m_spacers.emplace(key, cell);
	return cell;
}

const FontReplacement::RasterGlyph& FontReplacement::GameTextLayout::GetRaster(Cluster& c) {
	const auto& sized = *m_shapingSized;
	const auto& glyphSource = *c.GlyphSource;
	if (!c.Key) {
		RasterKey key{
			.Font = sized.Font,
			.Px = sized.Px,
			.Element = glyphSource.Element,
			.Source = c.Source,
			.EmSize = c.Px,
			.Origin = c.Origin,
			.RawAdvance = c.RawAdvance,
			.Glyphs = c.GlyphIds,
			.Advances = c.Advances,
		};
		for (const auto& o : c.Offsets)
			key.Offsets.emplace_back(o.Advance, o.Ascender);
		c.Key = std::move(key);
	}

	if (const auto it = m_rasters.find(*c.Key); it != m_rasters.end())
		return it->second;

	const GlyphRun run{c.GlyphIds, c.Advances, c.Offsets};
	auto raster = c.Source->Rasterize(c.Px, run, {.OriginX = c.Origin, .Advance = c.RawAdvance}).value_or(RasterGlyph{c.RawAdvance});
	raster = glyphSource.Element->Wrap(std::move(raster), sized.Px, glyphSource.Font->GetAlignment(sized.Px, glyphSource.Game), *c.Source, run, c.Origin);

	// The edge margin puts ink left of the pen, so the cell starts earlier (PlaceClusters), keeping the spacing.
	raster = m_replacer.FinishGlyph(sized, std::move(raster));
	if (m_rasters.size() >= MaxCachedRasters)
		m_rasters.clear();
	return m_rasters.emplace(*c.Key, std::move(raster)).first->second;
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::GetCell(Cluster& c, const uint8_t* bytes, int pad, int advance) {
	const auto& sized = *m_shapingSized;
	const auto& raster = GetRaster(c);
	const auto key = std::make_tuple(*c.Key, &sized, pad, advance);
	if (const auto it = m_cells.find(key); it != m_cells.end())
		return it->second;

	auto placed = raster;
	placed.Advance = advance;
	placed.Left += pad;

	// A cell that couldn't move left is drawn right of where shaping put its margin, so what follows moves along to keep its distance to the ink.
	if (placed.Width != 0 && placed.Left < 0)
		placed.Advance += (std::min)(-placed.Left, m_replacer.GetEdgeMargin(sized));
	const auto b = m_textToByte[c.TextStart];
	const auto utf8 = b < 0 ? 0u : GameUtf8::Pack(bytes + b);
	const auto cell = m_replacer.PlaceCell(sized, std::move(placed), utf8);
	if (c.RealItalic)
		m_replacer.MarkRealItalic(cell);
	m_cells.emplace(key, cell);
	return cell;
}
