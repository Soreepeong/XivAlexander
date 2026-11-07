#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::GameGlyph* FontReplacement::FontReplacer::CreateGlyph(SizedFont& sized, uint32_t utf8Value) {
	char32_t codepoint;
	if (!GameUtf8::TryUnpack(utf8Value, codepoint))
		return nullptr;
	const auto pick = sized.Pick(std::span(&codepoint, 1));
	if (pick.Element && pick.Element->DrawsGame())
		return nullptr;
	auto r = pick.Font->TryRasterize(codepoint, sized.Px, pick.Game, sized.Bold);
	return r ? PlaceCell(sized, FinishGlyph(sized, std::move(*r)), utf8Value) : nullptr;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetGameGlyph(SizedFont& sized, uint32_t utf8Value) {
	if (const auto found = sized.GameGlyphs.Find(utf8Value, m_frame))
		return *found;

	GameGlyph* glyph = nullptr;
	if (char32_t codepoint; GameUtf8::TryUnpack(utf8Value, codepoint)) {
		for (const auto font : sized.Fonts | std::views::values) {
			if ((glyph = PlaceGameGlyph(sized, font, font->IsReady() ? font->FindGlyph(codepoint) : nullptr, utf8Value)))
				break;
		}
	}

	return sized.GameGlyphs.Add(utf8Value, Keep(glyph), m_frame);
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::ConvertGameGlyph(SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value) {
	const auto key = std::make_pair(font, game);
	if (const auto found = sized.ConvertedGlyphs.Find(key, m_frame))
		return *found;

	auto glyph = PlaceGameGlyph(sized, font, game, utf8Value);
	if (!glyph) {
		const auto advance = static_cast<int>(StdRound(static_cast<float>(game->Width + game->OffsetX) * GetGameScale(sized, font)));
		glyph = PlaceCell(sized, RasterGlyph{advance}, utf8Value);
	}
	return sized.ConvertedGlyphs.Add(key, Keep(glyph), m_frame);
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
	ReadGameGlyphs();

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

	// After the game reads, which hold glyphs without a reference.
	SweepCaches();
	FreeUnreferenced();
	m_frame++;
}

void FontReplacement::FontReplacer::ReadGameGlyphs() {
	for (const auto glyph : std::exchange(m_gameReads, {})) {
		auto& cell = CellOf(glyph);
		try {
			const auto& s = *cell.Source;
			auto pixels = m_gameTextures.Read(s.Texture, s.X, s.Y, s.Width, s.Height, s.Plane);
			cell.Raster = WithSourcePixels(cell.Raster, s, std::move(pixels));
			WriteCell(glyph, cell);
		} catch (const std::exception& e) {
			Host::Error("Reading a game glyph's pixels failed: {}", e.what());
		}

		cell.Source.reset();
	}
}

void FontReplacement::FontReplacer::SweepCaches() {
	const auto release = [this](GameGlyph* glyph) { Release(glyph); };
	m_layout->Sweep(m_frame);
	for (const auto& sized : m_sizedFonts | std::views::values) {
		sized->Glyphs.Sweep(m_frame, release);
		sized->GameGlyphs.Sweep(m_frame, release);
		sized->ConvertedGlyphs.Sweep(m_frame, release);
		sized->WrapCells.Sweep(m_frame, release);
	}
}

void FontReplacement::FontReplacer::FreeUnreferenced() {
	for (const auto glyph : std::exchange(m_unreferenced, {})) {
		auto& slot = SlotOf(glyph);
		if (slot.Refs != 0) {
			slot.Unreferenced = false;
			continue;
		}
		if (m_frame - slot.Handed < GraceFrames) {
			m_unreferenced.push_back(glyph);
			continue;
		}

		// Its blank then waits for its own grace period.
		const auto blank = slot.Cell.Blank;
		slot = {};
		m_free.push_back(&slot);
		Release(blank);
	}
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::Keep(GameGlyph* glyph) {
	if (IsOwn(glyph))
		SlotOf(glyph).Refs++;
	return glyph;
}

void FontReplacement::FontReplacer::Release(GameGlyph* glyph) {
	if (!IsOwn(glyph))
		return;
	auto& slot = SlotOf(glyph);
	if (--slot.Refs == 0 && !slot.Unreferenced) {
		slot.Unreferenced = true;
		m_unreferenced.push_back(glyph);
	}
}

uint32_t FontReplacement::FontReplacer::RefCount(GameGlyph* glyph) const {
	return IsOwn(glyph) ? SlotOf(glyph).Refs : 0;
}

void FontReplacement::FontReplacer::Hand(GameGlyph* glyph) {
	if (IsOwn(glyph))
		SlotOf(glyph).Handed = m_frame;
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

	// Text is measured far more than drawn (every text change, at the node's unscaled size too), so a glyph is placed only once it is drawn
	// (WithPixels, which every glyph handed to the game goes through).
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
		const auto page = glyph->TextureIndex();
		const auto plane = glyph->Channel();
		if (cell.Atlas->Touch(page, plane, cell.Generation, m_frameTick))
			return glyph;

		// Its plane was reused; it is placed again from its pixels, which show in this frame like any newly placed glyph's, so that text baked
		// now (nameplates) keeps them.
		cell.State = CellState::Pending;
	}

	if (cell.State != CellState::Pending)
		return glyph;
	if (!cell.Atlas->CanHold(glyph->Width, glyph->Height)) {
		DropCell(glyph, cell);
		return glyph;
	}

	try {
		if (WritePixels(glyph, cell))
			return glyph;
	} catch (const std::exception& e) {
		Host::Error("Placing a glyph in the atlas failed: {}", e.what());
		DropCell(glyph, cell);
		return glyph;
	}

	m_fullAtlases.insert(cell.Atlas);
	return GetBlank(glyph);
}

bool FontReplacement::FontReplacer::WritePixels(GameGlyph* glyph, Cell& cell) {
	GlyphAtlas::Allocation a;
	if (!cell.Atlas->TryAllocate(glyph->Width, glyph->Height, m_frameTick, a))
		return false;

	glyph->Packed = GameGlyph::Pack(a.X, a.Y, a.Plane, a.Page);
	cell.State = CellState::Placed;
	cell.Generation = a.Generation;

	if (cell.Source)
		m_gameReads.push_back(glyph);
	else
		WriteCell(glyph, cell);
	return true;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetBlank(GameGlyph* glyph) {
	if (const auto blank = CellOf(glyph).Blank)
		return blank;
	const auto b = AllocateGlyph();
	*b = *glyph;
	DropPixels(b);
	CellOf(glyph).Blank = Keep(b);
	return b;
}

void FontReplacement::FontReplacer::EvictPlane(GlyphAtlas* atlas) {
	const auto plane = atlas->RecycleLeastRecentlyDrawn();
	m_fullAtlases.erase(atlas);
	m_lastEviction[atlas] = GetTickCount64();
	Host::Information("The {} glyph atlas was full: reusing plane {}, whose glyphs are placed again as they are drawn", atlas->Name(), plane);
}

void FontReplacement::FontReplacer::WriteCell(const GameGlyph* glyph, const Cell& cell) {
	const auto& r = cell.Raster;

	// Ink rows are cut off only past the game's byte-sized fields.
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
	// OffsetX takes the width, so that the advance stays as measured.
	glyph->OffsetX = static_cast<int8_t>(std::clamp(glyph->OffsetX + glyph->Width, -128, 127));
	glyph->Width = 0;
}

void FontReplacement::FontReplacer::DropGameReads(const std::set<uintptr_t>& textures) {
	std::erase_if(m_gameReads, [&](GameGlyph* glyph) {
		auto& cell = CellOf(glyph);
		if (!cell.Source || !textures.contains(cell.Source->Texture))
			return false;
		ReleasePixels(cell);
		return true;
	});
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::AllocateGlyph() {
	if (m_free.empty())
		return &m_slots.emplace_back().Glyph;
	const auto slot = m_free.back();
	m_free.pop_back();
	return &slot->Glyph;
}
