#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/CellCache.h"

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphBox.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphFactory.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::CellCache::CellCache(GlyphStore& store, GlyphFactory& factory, const FrameState& state)
	: m_store(store)
	, m_factory(factory)
	, m_cells(state)
	, m_spacers(state)
	, m_rasters(state) {
}

FontReplacement::GameGlyph* FontReplacement::CellCache::GetSpacer(const SizedFont& sized, int advance) {
	const auto key = std::make_pair(&sized, advance);
	if (const auto found = m_spacers.Find(key))
		return *found;
	return m_spacers.Add(key, m_factory.Spacer(sized, advance));
}

int FontReplacement::CellCache::GetEdgeMargin(const SizedFont& sized) const {
	return m_factory.GetEdgeMargin(sized);
}

const FontReplacement::RasterKey& FontReplacement::CellCache::KeyOf(const SizedFont& sized, Cluster& c) {
	if (!c.Key)
		c.Key = RasterKey::Of(sized, c);
	return *c.Key;
}

bool FontReplacement::CellCache::NeedsRaster(const SizedFont& sized, Cluster& c) {
	if (c.Raster)
		return false;
	if (c.Source->DrawsBitmaps())
		return true;
	const auto found = m_rasters.Find(KeyOf(sized, c));
	if (!found)
		return true;
	c.Raster = *found;
	return false;
}

const FontReplacement::RasterGlyph& FontReplacement::CellCache::GetRaster(const SizedFont& sized, Cluster& c) {
	return NeedsRaster(sized, c) ? AddRaster(sized, c, c.Rasterize()) : *c.Raster;
}

const FontReplacement::RasterGlyph& FontReplacement::CellCache::AddRaster(const SizedFont& sized, Cluster& c, std::optional<RasterGlyph> raw) {
	const auto& glyphSource = *c.GlyphSource;
	const GlyphRun run{c.GlyphIds, c.Advances, c.Offsets};
	auto raster = glyphSource.Element->Wrap(std::move(raw).value_or(RasterGlyph{c.RawAdvance}), sized.Px, KeyOf(sized, c).Alignment, *c.Source, run, c.Origin);

	// The edge margin puts ink left of the pen, so the cell starts earlier (Shaper::PlaceClusters), keeping the spacing.
	c.Raster = std::make_shared<const RasterGlyph>(m_factory.Finish(sized, std::move(raster)));
	if (!c.Source->DrawsBitmaps())
		m_rasters.Add(*c.Key, c.Raster);
	return *c.Raster;
}

FontReplacement::GameGlyph* FontReplacement::CellCache::GetCell(const SizedFont& sized, Cluster& c, const uint8_t* bytes, int b, int pad, int advance) {
	const auto& raster = GetRaster(sized, c);
	const auto key = std::make_tuple(*c.Key, &sized, pad, advance);
	if (const auto found = m_cells.Find(key))
		return *found;

	auto placed = raster;
	placed.Advance = advance;
	placed.Left += pad;

	// A cell that couldn't move left is drawn right of where shaping put its margin, so what follows moves along to keep its distance to the ink.
	if (const auto overhang = GlyphBox::InkLeftOfPen(placed); overhang > 0)
		placed.Advance += (std::min)(overhang, m_factory.GetEdgeMargin(sized));
	const auto utf8 = b < 0 ? 0u : GameUtf8::Pack(bytes + b);
	const auto cell = m_factory.FromRaster(sized, std::move(placed), utf8);
	if (c.RealItalic)
		m_store.CellOf(cell).RealItalic = true;
	return m_cells.Add(key, cell);
}

void FontReplacement::CellCache::Sweep() {
	const auto heldOnce = [this](GameGlyph* glyph) { return m_store.HeldOnce(glyph); };
	const auto release = [this](GameGlyph* glyph) { m_store.Release(glyph); };
	m_cells.Sweep(heldOnce, release);
	m_spacers.Sweep(heldOnce, release);
	m_rasters.Sweep([](const auto&) {});
}

void FontReplacement::CellCache::Clear() {
	const auto release = [this](GameGlyph* glyph) { m_store.Release(glyph); };
	m_cells.Clear(release);
	m_spacers.Clear(release);
	m_rasters.Clear([](const auto&) {});
}
