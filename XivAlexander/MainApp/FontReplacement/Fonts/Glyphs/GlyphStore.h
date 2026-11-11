#pragma once

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Fonts/GlyphAtlas.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A rect of a game font texture's plane, whose top left is at (0, Top) from the pen in the game font's pixels.
	struct GameTextureSource {
		uintptr_t Texture;
		int X;
		int Y;
		int Width;
		int Height;
		int Plane;
		int Top;
		float Scale;
	};

	// Where a glyph's pixels are.
	enum class CellStage : uint8_t {
		// No pixels, as of an empty glyph or a spacer.
		Empty,

		// Its pixels wait for room in the atlas, which they get when it is drawn.
		Pending,

		// Placed, its pixels read from the game's texture at the next Present.
		AwaitingRead,

		Placed,

		// Its pixels couldn't be placed or read, and it draws nothing at the same advance.
		Dropped,
	};

	// A glyph's pixels and lifetime. The methods are the only way between its stages.
	// A glyph with a box samples its atlas's BlankSlot while not IsPlaced.
	struct Cell {
		CellStage Stage = CellStage::Empty;
		GlyphAtlas* Atlas = nullptr;

		// Kept once placed, so that it can be placed again after its plane is reused. A game glyph's is blank until its pixels are read.
		RasterGlyph Raster;

		// The box's left relative to the pen, as Raster's Left is.
		int BoxLeft = 0;

		// The ink's top row in the box.
		int InkTop = 0;

		// While pending or awaiting its read.
		std::optional<GameTextureSource> Source;

		// Of its plane when placed.
		uint32_t Generation = 0;

		// A real italic glyph, which the game must not shear.
		bool RealItalic = false;

		// Caches and shaped runs holding it.
		int Refs = 0;

		// The frame it was last handed to the game in.
		uint64_t Handed = 0;

		// In GlyphStore's list of unreferenced glyphs.
		bool Queued = false;

		[[nodiscard]] bool IsPlaced() const { return Stage == CellStage::Placed || Stage == CellStage::AwaitingRead; }

		// For a glyph with a box, whose pixels are placed when it is drawn.
		void Pend(GameGlyph* glyph, GlyphAtlas* atlas, RasterGlyph raster, int boxLeft, int inkTop, std::optional<GameTextureSource> source);

		// Writes its pixels, or leaves them to be read if they come from the game.
		void Place(GameGlyph* glyph, const GlyphAtlas::Allocation& a);

		void PixelsRead(GameGlyph* glyph, RasterGlyph raster);

		// Its plane was reused, so it is placed again when next drawn, and until then draws none of what the plane holds now.
		void MarkStale(GameGlyph* glyph);

		void Drop(GameGlyph* glyph);

		// Marks its plane drawn in frame. Returns whether the plane still has its pixels.
		[[nodiscard]] bool Touch(const GameGlyph* glyph, uint64_t frame) const;

	private:
		// The glyph draws nothing in its box, at the same advance. Only for a cell with pixels, which has an atlas.
		void PointAtBlank(GameGlyph* glyph) const;

		void Write(const GameGlyph* glyph) const;
	};

	// Every glyph handed to the game, and how long each lives.
	// A glyph lives while something of ours holds it (Keep), and for GraceFrames after it was last handed to the game, which may keep its
	// pointer that long. nullptr and EmptyGlyph are ignored by every method.
	class GlyphStore {
	public:
		static constexpr uint64_t GraceFrames = 8;

	private:
		static constexpr int SlotBlockSize = 1024;

		// Glyph pointers handed to the game point into these. Id indexes the cells, or is -1 for the empty glyph.
		struct GlyphSlot {
			GameGlyph Glyph;
			int Id;
		};

		// The game reads glyphs from these blocks, so they must not move. Freed slots are reused.
		std::vector<std::unique_ptr<GlyphSlot[]>> m_slotBlocks;
		std::vector<Cell> m_cells;
		std::vector<int> m_freeIds;

		// Glyphs nothing of ours holds anymore.
		std::vector<int> m_unreferenced;

		GlyphSlot m_emptyGlyph{{}, -1};

	public:
		GlyphStore() = default;
		GlyphStore(const GlyphStore&) = delete;
		GlyphStore& operator=(const GlyphStore&) = delete;

		// A glyph of no width, for the characters of a shaped cluster after its first.
		[[nodiscard]] GameGlyph* EmptyGlyph() { return &m_emptyGlyph.Glyph; }

		[[nodiscard]] static bool IsOwn(const GameGlyph* glyph) { return glyph && SlotOf(glyph).Id >= 0; }

		// Reuses a freed slot if there is one. The glyph is held once by the caller.
		GameGlyph* Allocate();

		void Keep(GameGlyph* glyph);

		void Release(GameGlyph* glyph);

		// Whether only the asking cache holds a glyph, so that one a run still has stays and isn't made again alongside.
		[[nodiscard]] bool HeldOnce(GameGlyph* glyph) const { return RefCount(glyph) == 1; }

		[[nodiscard]] int RefCount(GameGlyph* glyph) const;

		// Marks it handed to the game in frame.
		void Hand(GameGlyph* glyph, uint64_t frame);

		// The reference is good until the next Allocate.
		[[nodiscard]] Cell& CellOf(const GameGlyph* glyph) { return m_cells[static_cast<size_t>(SlotOf(glyph).Id)]; }
		[[nodiscard]] const Cell& CellOf(const GameGlyph* glyph) const { return m_cells[static_cast<size_t>(SlotOf(glyph).Id)]; }

		// After the game's reads, which hold glyphs without a reference.
		void FreeUnreferenced(uint64_t frame);

	private:
		[[nodiscard]] static const GlyphSlot& SlotOf(const GameGlyph* glyph) { return *CONTAINING_RECORD(glyph, const GlyphSlot, Glyph); }

		[[nodiscard]] GlyphSlot& SlotAt(int id) { return m_slotBlocks[id / SlotBlockSize][id % SlotBlockSize]; }
	};
}
