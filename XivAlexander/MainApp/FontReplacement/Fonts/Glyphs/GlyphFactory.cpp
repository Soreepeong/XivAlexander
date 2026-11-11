#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphFactory.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFont.h"
#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Fonts/SizedFont.h"
#include "MainApp/FontReplacement/Game/TextureReader.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

int FontReplacement::GlyphFactory::GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px) {
	return (std::max)(0, static_cast<int>(std::ceil(edge.GetReach(px))) - 1);
}

int FontReplacement::GlyphFactory::GetEdgeMargin(const SizedFont& sized) const {
	return GetEdgeMargin(m_edge, sized.Px);
}

FontReplacement::RasterGlyph FontReplacement::GlyphFactory::Finish(const SizedFont& sized, RasterGlyph r) const {
	const auto m = GetEdgeMargin(sized);
	return m == 0 || r.Width == 0 ? r : r.Reboxed(r.Left - m, r.Top - m, r.Width + 2 * m, r.Height + 2 * m);
}

FontReplacement::GameGlyph* FontReplacement::GlyphFactory::FromRaster(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source) {
	// The game has no left bearing, so overhanging ink moves right into the box. Past 255 wide, it is cut on the right.
	const auto boxLeft = (std::min)(r.Left, 0);
	const auto width = r.Width == 0 ? 0 : (std::min)(r.Left + r.Width - boxLeft, 255);

	// A box is a full line high, as the game's are, so that italics slant alike. Ink beyond the line grows the box.
	const auto inkTop = sized.Ascent + r.Top;
	auto top = 0;
	auto height = sized.LineHeight;
	if (width != 0) {
		top = std::clamp(inkTop, -128, 0);
		height = (std::min)((std::max)(sized.LineHeight, inkTop + r.Height) - top, 255);
	}

	const auto glyph = m_store.Allocate();
	glyph->Utf8Value = utf8Value;
	glyph->Packed = GameGlyph::Pack(0, 0, 0, 0);
	glyph->Width = static_cast<uint8_t>(width);
	glyph->Height = static_cast<uint8_t>(height);
	glyph->OffsetX = static_cast<int8_t>(std::clamp(r.Advance - width, -128, 127));
	glyph->OffsetY = static_cast<int8_t>(top);

	// Text is measured far more than drawn (every text change, at the node's unscaled size too), so a glyph is placed only once it is drawn.
	if (width != 0)
		m_store.CellOf(glyph).Pend(sized.Atlas, std::move(r), boxLeft, inkTop - top, source);
	return glyph;
}

FontReplacement::GameGlyph* FontReplacement::GlyphFactory::Spacer(const SizedFont& sized, int advance, uint32_t utf8Value) {
	return FromRaster(sized, RasterGlyph{advance}, utf8Value);
}

FontReplacement::GameGlyph* FontReplacement::GlyphFactory::FromCodepoint(SizedFont& sized, uint32_t utf8Value) {
	char32_t codepoint;
	if (!GameUtf8::TryUnpack(utf8Value, codepoint))
		return nullptr;
	const auto pick = sized.Pick(std::span(&codepoint, 1));
	if (pick.Element && pick.Element->DrawsGame())
		return nullptr;
	auto r = pick.Font->TryRasterize(codepoint, sized.Px, pick.Game, sized.Bold);
	return r ? FromRaster(sized, Finish(sized, std::move(*r)), utf8Value) : nullptr;
}

FontReplacement::GameGlyph* FontReplacement::GlyphFactory::GameGlyphOf(const SizedFont& sized, uint32_t utf8Value) {
	char32_t codepoint;
	if (!GameUtf8::TryUnpack(utf8Value, codepoint))
		return nullptr;
	for (const auto font : sized.Fonts | std::views::values) {
		if (const auto glyph = FromGameGlyph(sized, font, font->IsReady() ? font->FindGlyph(codepoint) : nullptr, utf8Value))
			return glyph;
	}
	return nullptr;
}

FontReplacement::GameGlyph* FontReplacement::GlyphFactory::Convert(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value) {
	if (const auto glyph = FromGameGlyph(sized, font, game, utf8Value))
		return glyph;
	return Spacer(sized, static_cast<int>(StdRound(static_cast<float>(game->Width + game->OffsetX) * GetGameScale(sized, font))), utf8Value);
}

float FontReplacement::GlyphFactory::GetGameScale(const SizedFont& sized, GameFont* font) {
	return font->Size() <= 0 || std::abs(sized.Px - font->Size()) <= 0.25f ? 1.f : sized.Px / font->Size();
}

FontReplacement::GameGlyph* FontReplacement::GlyphFactory::FromGameGlyph(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value) {
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

	// Its box starts at the pen, with room for the edge in its own empty columns. A margin left of it would move it right.
	auto finished = Finish(sized, std::move(r));
	if (finished.Left < 0 && finished.Width + finished.Left > 0)
		finished = finished.Reboxed(0, finished.Top, finished.Width + finished.Left, finished.Height);
	return FromRaster(sized, std::move(finished), utf8Value, pixels);
}
