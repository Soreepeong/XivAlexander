#pragma once

#include "MainApp/FontReplacement/FontStructs.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FrameState;
	class GlyphAtlas;
	class KernelTextureWrapper;
	class SizedFont;
	class SizedFontRegistry;

	class CopyInfo {
		SizedFont* m_sized;
		SizedFont* m_bold = nullptr;
		bool m_boldFailed = false;

		// The sized fonts of the UI scales wrapping last measured with, and what they were for.
		std::vector<SizedFont*> m_wrapSizes;
		float m_wrapSizeY = 0;
		bool m_wrapBold = false;

	public:
		GameFont* const Original;
		const int HalfPx;

		// The CopyFonts generation it is up to date with.
		int Generation = 0;

		CopyInfo(GameFont* original, int halfPx, SizedFont* sized)
			: m_sized(sized)
			, Original(original)
			, HalfPx(halfPx) {}

		[[nodiscard]] SizedFont* Sized() const { return m_sized; }

		void Resize(SizedFont* value);

		// Its bold is drawn upright from now on.
		void BoldFailed() { m_boldFailed = true; }

		// Made on its first use.
		SizedFont* GetBold(SizedFontRegistry& registry);

		// The sized fonts of scales times sizeY, drawn last for the largest. The span is good while the arguments are the same.
		std::span<SizedFont* const> GetWrapSizes(SizedFontRegistry& registry, std::span<const float> scales, float sizeY, bool bold, SizedFont* drawn);
	};

	// Copies of the game's fonts at the sizes text is drawn at, which font sets are made to pick.
	// After MarkStale, each copy takes its new sized font and borrows from its game font again when next used, as every font set picks again
	// before drawing.
	class CopyFonts {
	public:
		// Sizes in half pixels. Below 4 px nothing is legible.
		static constexpr int MinHalfPx = 8;
		static constexpr int MaxHalfPx = 2 * 255;

	private:
		// A glyph's box is a line high and at most 255 pixels. Text whose lines are taller is drawn from a smaller copy, scaled up by the
		// renderer.
		static constexpr int MaxDrawnLineHeight = 255;

		// A size asked for in fewer frames in a row is drawn from the nearest copy, scaled by the renderer, as the sizes a scale animation
		// passes through are each asked for a frame or so.
		static constexpr int SettleFrames = 4;

		struct PickedCopy {
			GameFont* Copy;
			int HalfPx;
		};

		struct Unsettled {
			uint64_t Frame;
			int Streak;
		};

		SizedFontRegistry& m_sized;
		KernelTextureWrapper& m_blankTexture;
		const FrameState& m_state;

		std::map<std::pair<GameFont*, int>, GameFont*> m_byKey;
		std::map<GameFont*, CopyInfo> m_copies;

		// Picks by the size asked for, which the size is clamped from.
		std::map<std::pair<GameFont*, int>, PickedCopy> m_picks;

		// The sizes asked for that aren't picked yet, with the last frame each was asked in and in how many frames in a row.
		std::map<std::pair<GameFont*, int>, Unsettled> m_unsettled;

		// The sizes of the copies made, by game font.
		std::map<GameFont*, std::vector<int>> m_madeSizes;

		int m_generation = 0;

		// The copy GetGlyph asked about last, as it asks per character.
		GameFont* m_lastCopy = nullptr;
		CopyInfo* m_lastInfo = nullptr;

	public:
		CopyFonts(SizedFontRegistry& sized, KernelTextureWrapper& blankTexture, const FrameState& state)
			: m_sized(sized)
			, m_blankTexture(blankTexture)
			, m_state(state) {}

		CopyFonts(const CopyFonts&) = delete;
		CopyFonts& operator=(const CopyFonts&) = delete;

		// Once no font set has one picked.
		~CopyFonts();

		[[nodiscard]] static int HalfPxOf(float px);

		// Has every font set pick its font again.
		static void ForgetPicked();

		// Brought up to date if stale. nullptr if font isn't a copy.
		CopyInfo* TryGet(GameFont* copy);

		// The game font a copy is of, else font.
		[[nodiscard]] GameFont* OriginalOf(GameFont* font) const;

		// The copy for text at px, made smaller if its lines would be too tall for glyph boxes. mayStandIn is whether a copy of another size may
		// stand in for one of a size not settled yet, which text laid out once and kept must not take. halfPx gets the copy's size, and clamped
		// whether it is of another size, to be scaled by the renderer.
		GameFont* Pick(GameFont* original, float px, bool mayStandIn, int& halfPx, bool& clamped);

		GameFont* GetOrCreate(GameFont* original, int halfPx);

		// The sized fonts or what copies borrow from their game fonts changed.
		// Copies borrow again at once, with their old sized fonts, whose atlases are only cleared or else freed (leaving the blank texture), so
		// that none keeps what the game freed. Finding the new sized fonts waits for each copy's next use.
		void MarkStale();

		// For the edge, which copies claim their texture width by.
		void SyncAll();

		// The atlas got a page.
		void ApplyPages(GlyphAtlas* atlas);

	private:
		// False once the size is settled, or if no copy of the font was made yet.
		bool TryStandIn(const std::pair<GameFont*, int>& key, PickedCopy& pick);

		void Resync(GameFont* copy, CopyInfo& info);

		// Borrows the game font's glyph map, glyphs and flags, keeping the copy's own size, metrics and texture slots. BuildFont frees what was
		// borrowed, so it is taken again then.
		void Sync(GameFont* copy, const CopyInfo& info);

		// The blank texture fills the slots after the pages, so that the renderer has a vertex buffer and a texture for every texture index.
		// The atlas's BlankSlot never gets a page.
		void ApplyPages(GameFont* copy, const GlyphAtlas& atlas) const;
	};
}
