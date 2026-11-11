#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::Cell::Pend(GlyphAtlas* atlas, RasterGlyph raster, int boxLeft, int inkTop, std::optional<GameTextureSource> source) {
	Stage = CellStage::Pending;
	Atlas = atlas;
	Raster = std::move(raster);
	BoxLeft = boxLeft;
	InkTop = inkTop;
	Source = source;
}

void FontReplacement::Cell::Place(GameGlyph* glyph, const GlyphAtlas::Allocation& a) {
	glyph->Packed = GameGlyph::Pack(a.X, a.Y, a.Plane, a.Page);
	Generation = a.Generation;
	if (Source) {
		Stage = CellStage::AwaitingRead;
		return;
	}

	Stage = CellStage::Placed;
	Write(glyph);
}

void FontReplacement::Cell::PixelsRead(GameGlyph* glyph, RasterGlyph raster) {
	Raster = std::move(raster);
	Source.reset();
	Stage = CellStage::Placed;
	Write(glyph);
}

void FontReplacement::Cell::MarkStale() {
	if (Stage == CellStage::Placed)
		Stage = CellStage::Pending;
}

void FontReplacement::Cell::Drop(GameGlyph* glyph) {
	Stage = CellStage::Dropped;
	Raster = {};
	Source.reset();

	// OffsetX takes the width, so that the advance stays as measured.
	glyph->OffsetX = static_cast<int8_t>(std::clamp(glyph->OffsetX + glyph->Width, -128, 127));
	glyph->Width = 0;
}

bool FontReplacement::Cell::Touch(const GameGlyph* glyph, uint64_t frame) const {
	return Atlas->Touch(glyph->TextureIndex(), glyph->Channel(), Generation, frame);
}

void FontReplacement::Cell::Write(const GameGlyph* glyph) const {
	const auto& r = Raster;

	// Ink rows are cut off only past the game's byte-sized fields.
	const auto firstRow = (std::max)(0, -InkTop);
	const auto lastRow = (std::min)(r.Height, glyph->Height - InkTop);
	if (lastRow <= firstRow)
		return;
	Atlas->Write(
		glyph->TextureIndex(),
		glyph->Channel(),
		glyph->X(),
		glyph->Y() + InkTop + firstRow,
		glyph->Width,
		lastRow - firstRow,
		std::span(r.Alpha).subspan(static_cast<size_t>(firstRow) * r.Width, static_cast<size_t>(lastRow - firstRow) * r.Width),
		r.Width,
		r.Left - BoxLeft,
		r.HasLcd()
			? std::span<const uint8_t>(r.Lcd).subspan(static_cast<size_t>(firstRow) * r.Width * 3, static_cast<size_t>(lastRow - firstRow) * r.Width * 3)
			: std::span<const uint8_t>());
}

FontReplacement::GameGlyph* FontReplacement::GlyphStore::Allocate() {
	int id;
	if (!m_freeIds.empty()) {
		id = m_freeIds.back();
		m_freeIds.pop_back();
	} else {
		id = static_cast<int>(m_cells.size());
		if (id % SlotBlockSize == 0)
			m_slotBlocks.push_back(std::make_unique<GlyphSlot[]>(SlotBlockSize));
		SlotAt(id).Id = id;
		m_cells.emplace_back();
	}

	m_cells[id].Refs = 1;
	return &SlotAt(id).Glyph;
}

FontReplacement::GameGlyph* FontReplacement::GlyphStore::Keep(GameGlyph* glyph) {
	if (IsOwn(glyph))
		CellOf(glyph).Refs++;
	return glyph;
}

void FontReplacement::GlyphStore::Release(GameGlyph* glyph) {
	if (!IsOwn(glyph))
		return;
	auto& cell = CellOf(glyph);
	if (--cell.Refs == 0 && !cell.Queued) {
		cell.Queued = true;
		m_unreferenced.push_back(SlotOf(glyph).Id);
	}
}

int FontReplacement::GlyphStore::RefCount(GameGlyph* glyph) const {
	return IsOwn(glyph) ? CellOf(glyph).Refs : 0;
}

void FontReplacement::GlyphStore::Hand(GameGlyph* glyph, uint64_t frame) {
	if (IsOwn(glyph))
		CellOf(glyph).Handed = frame;
}

void FontReplacement::GlyphStore::FreeUnreferenced(uint64_t frame) {
	const auto count = m_unreferenced.size();
	for (size_t i = 0; i < count; i++) {
		const auto id = m_unreferenced[i];
		auto& cell = m_cells[id];
		if (cell.Refs != 0) {
			cell.Queued = false;
			continue;
		}

		if (frame - cell.Handed < GraceFrames) {
			m_unreferenced.push_back(id);
			continue;
		}

		// Its blank then waits for its own grace period.
		const auto blank = cell.Blank;
		cell = {};
		SlotAt(id) = {{}, id};
		m_freeIds.push_back(id);
		Release(blank);
	}

	m_unreferenced.erase(m_unreferenced.begin(), m_unreferenced.begin() + static_cast<ptrdiff_t>(count));
}
