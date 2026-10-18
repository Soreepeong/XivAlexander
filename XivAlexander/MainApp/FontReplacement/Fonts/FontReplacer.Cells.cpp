#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::GameGlyph* FontReplacement::FontReplacer::CreateGlyph(SizedFont& sized, uint32_t utf8Value) {
	char32_t codepoint;
	if (!GameUtf8::TryUnpack(utf8Value, codepoint))
		return nullptr;
	const auto pick = sized.Pick(std::span(&codepoint, 1));
	if (pick.Element && pick.Element->DrawsGame())
		return nullptr;
	auto r = pick.Font->TryRasterize(codepoint, sized.Px, pick.Game);
	return r ? PlaceCell(sized, FinishGlyph(sized, std::move(*r)), utf8Value) : nullptr;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetGameGlyph(SizedFont& sized, uint32_t utf8Value) {
	if (const auto it = sized.GameGlyphs.find(utf8Value); it != sized.GameGlyphs.end())
		return it->second;

	GameGlyph* glyph = nullptr;
	if (char32_t codepoint; GameUtf8::TryUnpack(utf8Value, codepoint)) {
		for (const auto font : sized.Sources) {
			if ((glyph = PlaceGameGlyph(sized, font, font->IsReady() ? font->FindGlyph(codepoint) : nullptr, utf8Value)))
				break;
		}
	}

	sized.GameGlyphs[utf8Value] = glyph;
	return glyph;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::ConvertGameGlyph(SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value) {
	const auto key = std::make_pair(font, game);
	if (const auto it = sized.ConvertedGlyphs.find(key); it != sized.ConvertedGlyphs.end())
		return it->second;

	auto glyph = PlaceGameGlyph(sized, font, game, utf8Value);
	if (!glyph) {
		const auto advance = static_cast<int>(StdRound(static_cast<float>(game->Width + game->OffsetX) * GetGameScale(sized, font)));
		glyph = PlaceCell(sized, RasterGlyph{advance}, utf8Value);
	}
	sized.ConvertedGlyphs[key] = glyph;
	return glyph;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::PlaceGameGlyph(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value) {
	if (!game)
		return nullptr;
	const auto textureIndex = game->TextureIndex();
	const auto texture = textureIndex < (std::min)(static_cast<int>(font->TextureCount()), GameFont::MaxTextures) ? font->GetTexture(textureIndex) : 0;
	if (!texture || !TextureReader::CanRead(texture))
		return nullptr;

	const auto scale = GetGameScale(sized, font);
	const auto top = game->OffsetY - font->Ascent();
	int left, t, w, h;
	RasterGlyph::ScaledBounds(0, top, game->Width, game->Height, scale, 0, 0, left, t, w, h);
	RasterGlyph r{static_cast<int>(StdRound(static_cast<float>(game->Width + game->OffsetX) * scale)), left, t, w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h)};
	const GameTextureSource pixels{texture, game->X(), game->Y(), game->Width, game->Height, game->Channel(), top, scale};

	// Its box starts at the pen, with room for the edge in its own empty columns: a margin left of it would move it right.
	auto finished = FinishGlyph(sized, std::move(r));
	if (finished.Left < 0 && finished.Width + finished.Left > 0)
		finished = finished.Reboxed(0, finished.Top, finished.Width + finished.Left, finished.Height);
	return PlaceCell(sized, std::move(finished), utf8Value, pixels);
}

float FontReplacement::FontReplacer::GetGameScale(const SizedFont& sized, GameFont* font) {
	return font->Size() <= 0 || std::abs(sized.Px - font->Size()) <= 0.25f ? 1.f : sized.Px / font->Size();
}

void FontReplacement::FontReplacer::Upload() {
	m_frameTick = GetTickCount64();
	for (const auto glyph : m_gameReads) {
		auto& cell = CellOf(glyph);
		try {
			const auto& s = *cell.Source;
			auto pixels = m_gameTextures.Read(s.Texture, s.X, s.Y, s.Width, s.Height, s.Plane);
			cell.Raster = WithSourcePixels(cell.Raster, s, std::move(pixels));
			WriteCell(glyph, cell);
		} catch (const std::exception& e) {
			Host::Error("Reading a game glyph's pixels failed: {}", e.what());
		}

		ReleasePixels(cell);
	}

	m_gameReads.clear();

	for (const auto atlas : std::vector(m_fullAtlases.begin(), m_fullAtlases.end())) {
		if (const auto it = m_lastEviction.find(atlas); it != m_lastEviction.end() && GetTickCount64() - it->second < EvictIntervalMs)
			continue;
		try {
			EvictPlane(atlas);
		} catch (const std::exception& e) {
			Host::Error("Making room in the {} glyph atlas failed; making every glyph again: {}", atlas->Name(), e.what());
			RebuildGlyphs();
			break;
		}
	}

	for (const auto& atlas : m_atlases | std::views::values)
		atlas->Upload();

	// Here, as the immediate context is free only before Present.
	if (!m_blankCleared && m_blankTexture) {
		const std::vector<uint8_t> zeros(static_cast<size_t>(BlankTextureSize) * BlankTextureSize * 4);
		GetImmediateContext(m_blankTexture->Resource())->UpdateSubresource(m_blankTexture->Resource(), 0, nullptr, zeros.data(), BlankTextureSize * 4, 0);
		m_blankCleared = true;
	}

	// This frame's UI draw commands are processed by now.
	m_subpixelShaders.EndFrame();
}

FontReplacement::RasterGlyph FontReplacement::FontReplacer::WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha) {
	RasterGlyph scaled{0, 0, s.Top, s.Width, s.Height, std::move(sourceAlpha)};
	if (s.Scale != 1)
		scaled = scaled.Scaled(s.Scale, 0, 0);
	auto res = box;
	res.Alpha.assign(static_cast<size_t>(box.Width) * box.Height, 0);
	RasterGlyph::BlitMax(res.Alpha, box.Width, box.Height, scaled, scaled.Left - box.Left, scaled.Top - box.Top);
	return res;
}

int FontReplacement::FontReplacer::GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px) {
	return (std::max)(0, static_cast<int>(std::ceil(edge.GetReach(px))) - 1);
}

FontReplacement::RasterGlyph FontReplacement::FontReplacer::FinishGlyph(const SizedFont& sized, RasterGlyph r) const {
	const auto m = GetEdgeMargin(sized);
	return m == 0 || r.Width == 0 ? r : r.Reboxed(r.Left - m, r.Top - m, r.Width + 2 * m, r.Height + 2 * m);
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::PlaceCell(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source) {
	// The game has no left bearing: overhanging ink moves right into the box; past 255 wide it's cut on the right.
	const auto boxLeft = (std::min)(r.Left, 0);
	const auto width = r.Width == 0 ? 0 : (std::min)(r.Left + r.Width - boxLeft, 255);

	// Boxes are a full line high like the game's, so that italics slant alike; ink beyond the line grows the box.
	const auto inkTop = sized.Ascent + r.Top;
	auto top = 0;
	auto height = sized.LineHeight;
	if (width != 0) {
		top = std::clamp(inkTop, -128, 0);
		height = (std::min)((std::max)(sized.LineHeight, inkTop + r.Height) - top, 255);
	}

	const auto glyph = AllocateGlyph();
	glyph->Utf8Value = utf8Value;
	glyph->Packed = GameGlyph::Pack(0, 0, 0, 0);
	glyph->Width = static_cast<uint8_t>(width);
	glyph->Height = static_cast<uint8_t>(height);
	glyph->OffsetX = static_cast<int8_t>(std::clamp(r.Advance - width, -128, 127));
	glyph->OffsetY = static_cast<int8_t>(top);

	// Text is measured far more than drawn (every change, at the unscaled size too): pixels wait here until drawn (WithPixels, which all glyphs pass).
	if (width != 0) {
		CellOf(glyph) = {
			.State = CellState::Pending,
			.Atlas = sized.Atlas,
			.Raster = std::move(r),
			.BoxLeft = boxLeft,
			.InkTop = inkTop - top,
			.Source = source,
		};
	}

	return glyph;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::WithPixels(GameGlyph* glyph) {
	if (s_current.MeasuringOnly || glyph == &m_emptyGlyph->Glyph)
		return glyph;

	auto& cell = CellOf(glyph);
	if (cell.State == CellState::Placed) {
		cell.Drawn = m_frameTick;
		return glyph;
	}

	if (cell.State != CellState::Pending)
		return glyph;
	if (!cell.Atlas->CanHold(glyph->Width, glyph->Height)) {
		DropCell(glyph, cell);
		return glyph;
	}

	try {
		if (WritePixels(glyph, cell, m_frameTick))
			return glyph;
	} catch (const std::exception& e) {
		Host::Error("Placing a glyph in the atlas failed: {}", e.what());
		DropCell(glyph, cell);
		return glyph;
	}

	m_fullAtlases.insert(cell.Atlas);
	return GetBlank(glyph);
}

bool FontReplacement::FontReplacer::WritePixels(GameGlyph* glyph, Cell& cell, uint64_t drawn) {
	int page, plane, x, y;
	if (!cell.Atlas->TryAllocate(glyph->Width, glyph->Height, page, plane, x, y))
		return false;

	glyph->Packed = GameGlyph::Pack(x, y, plane, page);
	cell.State = CellState::Placed;
	cell.Drawn = drawn;
	if (cell.Source) {
		m_gameReads.push_back(glyph);
	} else {
		WriteCell(glyph, cell);
		cell.Raster = {};
	}
	return true;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetBlank(GameGlyph* glyph) {
	if (const auto blank = CellOf(glyph).Blank)
		return blank;
	const auto b = AllocateGlyph();
	*b = *glyph;
	DropPixels(b);
	CellOf(glyph).Blank = b;
	return b;
}

void FontReplacement::FontReplacer::EvictPlane(GlyphAtlas* atlas) {
	const auto now = GetTickCount64();
	const auto planes = static_cast<size_t>(atlas->MaxPlanes());
	std::vector<int64_t> stale(planes);
	std::vector<uint64_t> oldest(planes, UINT64_MAX);
	for (const auto& [glyph, cell] : m_slots) {
		if (cell.State != CellState::Placed || cell.Atlas != atlas)
			continue;
		const auto plane = PlaneOf(&glyph);
		if (now - cell.Drawn > KeepDrawnMs)
			stale[plane] += GlyphAtlas::PaddedArea(glyph.Width, glyph.Height);
		oldest[plane] = (std::min)(oldest[plane], cell.Drawn);
	}

	// The most stale room; without any, the plane whose glyphs were drawn least recently.
	auto victim = static_cast<int>(std::ranges::max_element(stale) - stale.begin());
	if (stale[victim] == 0)
		victim = static_cast<int>(std::ranges::min_element(oldest) - oldest.begin());

	struct Moved {
		GlyphSlot* Slot;
		uint64_t Drawn;
		std::vector<uint8_t> Pixels;
		std::vector<uint8_t> Lcd;
	};
	std::vector<Moved> moved;
	for (auto& slot : m_slots) {
		const auto glyph = &slot.Glyph;
		if (slot.Cell.State != CellState::Placed || slot.Cell.Atlas != atlas || PlaneOf(glyph) != victim)
			continue;
		const auto page = glyph->TextureIndex();
		moved.push_back({
			&slot,
			slot.Cell.Drawn,
			atlas->Read(page, glyph->Channel(), glyph->X(), glyph->Y(), glyph->Width, glyph->Height),
			atlas->Subpixel() ? atlas->ReadLcd(page, glyph->X(), glyph->Y(), glyph->Width, glyph->Height) : std::vector<uint8_t>{},
		});
	}

	std::ranges::sort(moved, [](const Moved& a, const Moved& b) { return a.Drawn > b.Drawn; });
	atlas->ClearPlane(victim / GlyphAtlas::PlanesPerPage, victim % GlyphAtlas::PlanesPerPage);

	// What may stay: the glyphs drawn recently, but no more than half of the plane if none were stale.
	auto budget = stale[victim] == 0 ? static_cast<int64_t>(atlas->Width()) * atlas->Height() / 2 : INT64_MAX;
	auto kept = 0;
	for (auto& [slot, drawn, pixels, lcd] : moved) {
		const auto glyph = &slot->Glyph;
		auto& cell = slot->Cell;
		cell.State = CellState::Pending;
		cell.Raster = {0, 0, 0, glyph->Width, glyph->Height, std::move(pixels), std::move(lcd)};
		cell.BoxLeft = 0;
		cell.InkTop = 0;
		glyph->Packed = GameGlyph::Pack(0, 0, 0, 0);
		const auto area = GlyphAtlas::PaddedArea(glyph->Width, glyph->Height);
		if (now - drawn <= KeepDrawnMs && area <= budget && WritePixels(glyph, cell, drawn)) {
			budget -= area;
			kept++;
		}
	}

	m_fullAtlases.erase(atlas);
	m_lastEviction[atlas] = now;
	Host::Information("The {} glyph atlas was full: emptied plane {}, kept {} recently drawn glyphs of its {}", atlas->Name(), victim, kept, moved.size());
}

int FontReplacement::FontReplacer::PlaneOf(const GameGlyph* glyph) {
	return glyph->TextureIndex() * GlyphAtlas::PlanesPerPage + glyph->Channel();
}

void FontReplacement::FontReplacer::WriteCell(const GameGlyph* glyph, const Cell& cell) {
	const auto& r = cell.Raster;

	// The ink's rows in the box (cut off only past the game's byte-sized fields).
	const auto firstRow = (std::max)(0, -cell.InkTop);
	const auto lastRow = (std::min)(r.Height, glyph->Height - cell.InkTop);
	if (lastRow > firstRow) {
		cell.Atlas->Write(
			glyph->TextureIndex(),
			glyph->Channel(),
			glyph->X(),
			glyph->Y() + cell.InkTop + firstRow,
			glyph->Width,
			lastRow - firstRow,
			std::span(r.Alpha).subspan(static_cast<size_t>(firstRow) * r.Width, static_cast<size_t>(lastRow - firstRow) * r.Width),
			r.Width,
			r.Left - cell.BoxLeft,
			r.HasLcd()
				? std::span<const uint8_t>(r.Lcd).subspan(static_cast<size_t>(firstRow) * r.Width * 3, static_cast<size_t>(lastRow - firstRow) * r.Width * 3)
				: std::span<const uint8_t>());
	}
}

void FontReplacement::FontReplacer::ReleasePixels(Cell& cell) {
	cell.Raster = {};
	cell.Source.reset();
}

void FontReplacement::FontReplacer::DropCell(GameGlyph* glyph, Cell& cell) {
	cell.State = CellState::None;
	ReleasePixels(cell);
	DropPixels(glyph);
}

void FontReplacement::FontReplacer::DropPixels(GameGlyph* glyph) {
	glyph->OffsetX = static_cast<int8_t>(std::clamp(glyph->OffsetX + glyph->Width, -128, 127));
	glyph->Width = 0;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::AllocateGlyph() {
	return &m_slots.emplace_back().Glyph;
}

void FontReplacement::FontReplacer::FreeGlyphs() {
	m_slots.clear();
}
