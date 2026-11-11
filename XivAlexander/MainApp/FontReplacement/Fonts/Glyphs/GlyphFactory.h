#pragma once

#include "Config/FontReplacementConfigs.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class SizedFont;

	// Makes glyphs, whose pixels are placed only once they are drawn. Every glyph returned is held once by the caller, unless it is nullptr.
	class GlyphFactory {
		GlyphStore& m_store;
		FontReplacementEdgeConfig m_edge;

	public:
		explicit GlyphFactory(GlyphStore& store)
			: m_store(store) {}

		// The edge, which glyphs get margins for.
		[[nodiscard]] const FontReplacementEdgeConfig& Edge() const { return m_edge; }
		void SetEdge(const FontReplacementEdgeConfig& value) { m_edge = value; }

		[[nodiscard]] static int GetEdgeMargin(const FontReplacementEdgeConfig& edge, float px);

		[[nodiscard]] int GetEdgeMargin(const SizedFont& sized) const;

		// Adds empty margins for a wide edge, as the edge is only drawn within a glyph's box and the pixel around it.
		[[nodiscard]] RasterGlyph Finish(const SizedFont& sized, RasterGlyph r) const;

		// With source, r is blank and its pixels are read from the game's texture once placed.
		GameGlyph* FromRaster(const SizedFont& sized, RasterGlyph r, uint32_t utf8Value, std::optional<GameTextureSource> source = std::nullopt);

		// Pixels of no ink that advance by advance.
		GameGlyph* Spacer(const SizedFont& sized, int advance, uint32_t utf8Value = 0);

		// nullptr leaves the glyph to the game.
		GameGlyph* FromCodepoint(SizedFont& sized, uint32_t utf8Value);

		// From the largest of the sized font's game fonts that has it, copied into the atlas because the edge steps by texels of the width all
		// of a font's textures share, which is the atlas's. nullptr if none has it.
		GameGlyph* GameGlyphOf(const SizedFont& sized, uint32_t utf8Value);

		// The game font's own glyph, which has a texture index of the game font's textures.
		GameGlyph* Convert(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);

	private:
		// 1 within the half-pixel rounding of copy sizes.
		[[nodiscard]] static float GetGameScale(const SizedFont& sized, GameFont* font);

		GameGlyph* FromGameGlyph(const SizedFont& sized, GameFont* font, const GameGlyph* game, uint32_t utf8Value);
	};
}
