#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include <execution>

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
	RasterizeConcurrently();
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
				: startsElement ? GetSpacer(*m_shapingSized, Share(advance, elements, element))
				: m_emptyGlyph;
		}
	}
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::GetSpacer(const SizedFont& sized, int advance) {
	const auto key = std::make_pair(&sized, advance);
	const auto frame = m_replacer.Frame();
	if (const auto found = m_spacers.Find(key, frame))
		return *found;
	return m_spacers.Add(key, m_replacer.Keep(m_replacer.PlaceCell(sized, RasterGlyph{advance}, 0)), frame);
}

void FontReplacement::GameTextLayout::RasterizeConcurrently() {
	const auto frame = m_replacer.Frame();
	std::vector<const Cluster*> pending;
	std::set<RasterKey> seen;
	for (auto& c : m_clusters) {
		if (c.Missing() || !c.Source->RasterizesConcurrently())
			continue;
		const auto& key = KeyOf(c);
		if (!m_rasters.Find(key, frame) && seen.insert(key).second)
			pending.push_back(&c);
	}
	if (pending.size() < 2)
		return;

	// A cluster that fails is rasterized again by GetRaster, which reports it.
	std::vector<std::optional<RasterGlyph>> raws(pending.size());
	std::vector<uint8_t> failed(pending.size());
	std::vector<size_t> indices(pending.size());
	std::iota(indices.begin(), indices.end(), size_t{});
	std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
		const auto& c = *pending[i];
		try {
			raws[i] = c.Source->Rasterize(c.Px, {c.GlyphIds, c.Advances, c.Offsets}, {.OriginX = c.Origin, .Advance = c.RawAdvance});
		} catch (...) {
			failed[i] = 1;
		}
	});

	for (size_t i = 0; i < pending.size(); i++) {
		if (!failed[i])
			AddRaster(*pending[i], std::move(raws[i]));
	}
}

const FontReplacement::GameTextLayout::RasterKey& FontReplacement::GameTextLayout::KeyOf(Cluster& c) {
	if (!c.Key) {
		const auto& sized = *m_shapingSized;
		RasterKey key{
			.Font = sized.Font,
			.Px = sized.Px,
			.Element = c.GlyphSource->Element,
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
	return *c.Key;
}

const FontReplacement::RasterGlyph& FontReplacement::GameTextLayout::GetRaster(Cluster& c) {
	if (const auto found = m_rasters.Find(KeyOf(c), m_replacer.Frame()))
		return *found;
	return AddRaster(c, c.Source->Rasterize(c.Px, {c.GlyphIds, c.Advances, c.Offsets}, {.OriginX = c.Origin, .Advance = c.RawAdvance}));
}

const FontReplacement::RasterGlyph& FontReplacement::GameTextLayout::AddRaster(const Cluster& c, std::optional<RasterGlyph> raw) {
	const auto& sized = *m_shapingSized;
	const auto& glyphSource = *c.GlyphSource;
	const GlyphRun run{c.GlyphIds, c.Advances, c.Offsets};
	auto raster = glyphSource.Element->Wrap(
		std::move(raw).value_or(RasterGlyph{c.RawAdvance}), sized.Px, glyphSource.Font->GetAlignment(sized.Px, glyphSource.Game), *c.Source, run, c.Origin);

	// The edge margin puts ink left of the pen, so the cell starts earlier (PlaceClusters), keeping the spacing.
	raster = m_replacer.FinishGlyph(sized, std::move(raster));
	return m_rasters.Add(*c.Key, std::move(raster), m_replacer.Frame());
}

FontReplacement::GameGlyph* FontReplacement::GameTextLayout::GetCell(Cluster& c, const uint8_t* bytes, int pad, int advance) {
	const auto& sized = *m_shapingSized;
	const auto& raster = GetRaster(c);
	const auto key = std::make_tuple(*c.Key, &sized, pad, advance);
	const auto frame = m_replacer.Frame();
	if (const auto found = m_cells.Find(key, frame))
		return *found;

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
	return m_cells.Add(key, m_replacer.Keep(cell), frame);
}
