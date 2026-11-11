#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/CellPlacer.h"

#include "MainApp/FontReplacement/Fonts/CopyFonts.h"
#include "MainApp/FontReplacement/Host.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::GameGlyph* FontReplacement::CellPlacer::WithPixels(GameGlyph* glyph, uint64_t frame) {
	if (!GlyphStore::IsOwn(glyph))
		return glyph;

	auto& cell = m_store.CellOf(glyph);
	if (cell.IsPlaced()) {
		if (cell.Touch(glyph, frame))
			return glyph;
		cell.MarkStale();
	}

	if (cell.Stage != CellStage::Pending)
		return glyph;
	const auto atlas = cell.Atlas;
	if (!atlas->CanHold(glyph->Width, glyph->Height)) {
		cell.Drop(glyph);
		return glyph;
	}

	try {
		GlyphAtlas::Allocation a;
		switch (atlas->TryAllocate(glyph->Width, glyph->Height, frame, a)) {
			case GlyphAtlas::AllocationResult::PlacedOnNewPage:
				m_copies.ApplyPages(atlas);
				[[fallthrough]];
			case GlyphAtlas::AllocationResult::Placed:
				cell.Place(glyph, a);
				if (cell.Stage == CellStage::AwaitingRead)
					m_pendingReads.push_back(glyph);
				return glyph;
			case GlyphAtlas::AllocationResult::Full:
				break;
		}
	} catch (const std::exception& e) {
		Host::Error("Placing a glyph in the atlas failed: {}", e.what());
		m_store.CellOf(glyph).Drop(glyph);
		return glyph;
	}

	m_full.insert(atlas);
	return GetBlank(glyph);
}

void FontReplacement::CellPlacer::ReadGameGlyphs() {
	for (const auto glyph : m_pendingReads) {
		auto& cell = m_store.CellOf(glyph);
		if (cell.Stage != CellStage::AwaitingRead)
			continue;
		try {
			const auto& s = *cell.Source;
			auto pixels = m_textures.Read(s.Texture, s.X, s.Y, s.Width, s.Height, s.Plane);
			cell.PixelsRead(glyph, WithSourcePixels(cell.Raster, s, std::move(pixels)));
		} catch (const std::exception& e) {
			Host::Error("Reading a game glyph's pixels failed: {}", e.what());
			cell.Drop(glyph);
		}
	}
	m_pendingReads.clear();
}

bool FontReplacement::CellPlacer::EvictFull() {
	const auto now = GetTickCount64();
	auto reused = false;
	for (const auto atlas : std::vector(m_full.begin(), m_full.end())) {
		if (const auto it = m_lastEviction.find(atlas); it != m_lastEviction.end() && now - it->second < EvictIntervalMs)
			continue;
		const auto plane = atlas->RecycleLeastRecentlyDrawn();
		m_full.erase(atlas);
		m_lastEviction[atlas] = now;
		reused = true;
		Host::Information("The {} glyph atlas was full: reusing plane {}, whose glyphs are placed again as they are drawn", atlas->Name(), plane);
	}
	return reused;
}

void FontReplacement::CellPlacer::Clear() {
	m_full.clear();
	m_lastEviction.clear();
	for (const auto glyph : m_pendingReads)
		m_store.CellOf(glyph).Drop(glyph);
	m_pendingReads.clear();
}

void FontReplacement::CellPlacer::ClearAll() {
	Clear();
	m_textures.Clear();
}

FontReplacement::RasterGlyph FontReplacement::CellPlacer::WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha) {
	RasterGlyph scaled{0, 0, s.Top, s.Width, s.Height, std::move(sourceAlpha)};
	if (s.Scale != 1)
		scaled = scaled.Scaled(s.Scale, 0, 0);
	auto res = box;
	res.Alpha.assign(static_cast<size_t>(box.Width) * box.Height, 0);
	RasterGlyph::BlitMax(res.Alpha, box.Width, box.Height, scaled, scaled.Left - box.Left, scaled.Top - box.Top);
	return res;
}

FontReplacement::GameGlyph* FontReplacement::CellPlacer::GetBlank(GameGlyph* glyph) {
	if (const auto blank = m_store.CellOf(glyph).Blank)
		return blank;
	const auto b = m_store.Allocate();
	*b = *glyph;
	m_store.CellOf(b).Drop(b);
	m_store.CellOf(glyph).Blank = b;
	return b;
}
