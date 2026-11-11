#pragma once

#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Game/TextureReader.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class CopyFonts;

	// Puts a glyph's pixels in its atlas when it is drawn, reads those of glyphs copied from the game's fonts at Present, and makes room in
	// full atlases.
	class CellPlacer {
		// When an atlas is full, its least recently drawn plane is reused, at most this often.
		static constexpr uint64_t EvictIntervalMs = 1000;

		GlyphStore& m_store;
		CopyFonts& m_copies;
		std::set<GlyphAtlas*> m_full;
		std::map<GlyphAtlas*, uint64_t> m_lastEviction;
		TextureReader m_textures;
		std::vector<GameGlyph*> m_pendingReads;

	public:
		CellPlacer(GlyphStore& store, CopyFonts& copies)
			: m_store(store)
			, m_copies(copies) {}

		CellPlacer(const CellPlacer&) = delete;
		CellPlacer& operator=(const CellPlacer&) = delete;

		// A glyph that finds no room draws nothing this time, at the same advance, and room is made at the next Present.
		// A glyph whose plane was reused is placed again here, showing in this frame, so that text baked now keeps it.
		GameGlyph* WithPixels(GameGlyph* glyph, uint64_t frame);

		// At Present, after this frame's glyphs were placed.
		void ReadGameGlyphs();

		// At Present, after this frame's text was laid out. The planes' glyphs are placed again as they are drawn. Returns whether a plane was
		// reused.
		bool EvictFull();

		// The glyphs waiting for their pixels are drawn blank.
		void Clear();

		// Also forgets the textures read, which the game may have let go.
		void ClearAll();

	private:
		[[nodiscard]] static RasterGlyph WithSourcePixels(const RasterGlyph& box, const GameTextureSource& s, std::vector<uint8_t> sourceAlpha);

		GameGlyph* GetBlank(GameGlyph* glyph);
	};
}
