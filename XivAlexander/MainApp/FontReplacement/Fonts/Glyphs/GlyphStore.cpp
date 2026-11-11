#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::GlyphSlotState::Pend(GameGlyph* glyph, GlyphAtlas* atlas, RasterGlyph raster, int boxLeft, int inkTop, std::optional<GameTextureSource> source) {
	Stage = GlyphPixelStage::Pending;
	Atlas = atlas;
	Raster = std::move(raster);
	BoxLeft = boxLeft;
	InkTop = inkTop;
	Source = source;
	PointAtBlank(glyph);
}

void FontReplacement::GlyphSlotState::Place(GameGlyph* glyph, const GlyphAtlas::Allocation& a) {
	glyph->Packed = GameGlyph::Pack(a.X, a.Y, a.Plane, a.Page);
	Generation = a.Generation;
	if (Source) {
		Stage = GlyphPixelStage::AwaitingRead;
		return;
	}

	Stage = GlyphPixelStage::Placed;
	Write(glyph);
}

void FontReplacement::GlyphSlotState::PixelsRead(GameGlyph* glyph, RasterGlyph raster) {
	Raster = std::move(raster);
	Source.reset();
	Stage = GlyphPixelStage::Placed;
	Write(glyph);
}

void FontReplacement::GlyphSlotState::MarkStale(GameGlyph* glyph) {
	if (Stage != GlyphPixelStage::Placed)
		return;
	Stage = GlyphPixelStage::Pending;
	PointAtBlank(glyph);
}

void FontReplacement::GlyphSlotState::Drop(GameGlyph* glyph) {
	Stage = GlyphPixelStage::Dropped;
	Raster = {};
	Source.reset();
	PointAtBlank(glyph);
}

bool FontReplacement::GlyphSlotState::Touch(const GameGlyph* glyph, uint64_t frame) const {
	return Atlas->Touch(glyph->TextureIndex(), glyph->Channel(), Generation, frame);
}

void FontReplacement::GlyphSlotState::PointAtBlank(GameGlyph* glyph) const {
	glyph->Packed = GameGlyph::Pack(0, 0, 0, Atlas->BlankSlot());
}

void FontReplacement::GlyphSlotState::Write(const GameGlyph* glyph) const {
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

void FontReplacement::GlyphStore::Keep(GameGlyph* glyph) {
	if (IsOwn(glyph))
		CellOf(glyph).Refs++;
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

		cell = {};
		SlotAt(id) = {{}, id};
		m_freeIds.push_back(id);
	}

	m_unreferenced.erase(m_unreferenced.begin(), m_unreferenced.begin() + static_cast<ptrdiff_t>(count));
}
