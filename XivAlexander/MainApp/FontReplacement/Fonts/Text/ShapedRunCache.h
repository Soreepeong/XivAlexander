#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/Fonts/Text/ClusterGlyphCache.h"
#include "MainApp/FontReplacement/Fonts/Text/Shaper.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class DirectWriteServices;
	class GlyphFactory;
	class GlyphStore;

	// The game lays text out a character at a time (LayOutCharacter), so a character no active run has is shaped with the rest of its line,
	// which later lookups are answered from.
	// A shaped run holds its glyphs while a cache has it. Active runs last for a frame, so that a cache sweep never takes one.
	class ShapedRunCache {
		static constexpr size_t MaxActiveRuns = 8;

		// A resized chat log lays out every line again at each wrapping size, which the runs of a long log must all be kept for.
		static constexpr size_t CachedRuns = 65536;

		using RunKey = std::tuple<uint64_t, int, const SizedFont*, ItalicMode>;

		struct ShapedRun {
			std::vector<GameGlyph*> Glyphs;

			// The text it was shaped from, which a run found by its hash must match.
			std::vector<uint8_t> Bytes;

			// The italics of every byte, unless they differ by byte.
			ItalicMode Italic = ItalicMode::Upright;

			// Whether each byte is in real italics, nullopt if all are alike.
			std::optional<std::vector<uint8_t>> ItalicBytes;

			ShapedRun(std::vector<GameGlyph*> glyphs, std::vector<uint8_t> bytes, ItalicMode italic, std::span<const uint8_t> italicBytes);

			[[nodiscard]] ItalicMode ItalicAt(size_t b) const;
		};

		// A run shaped at an address, used only while the text there is unchanged since shaping, as the game reuses its text buffers.
		// Text is laid out forward, so the text is compared again only where a lookup isn't past the last one, as where a buffer's text is laid
		// out again.
		class ActiveRun {
			const uint8_t* m_start = nullptr;
			const uint8_t* m_end = nullptr;
			const uint8_t* m_last = nullptr;
			std::shared_ptr<const ShapedRun> m_shaped;

			// The byte that ended the run is compared too, as what follows the last character could join its cluster.
			size_t m_compareLength = 0;
			uint8_t m_endByte = 0;

		public:
			const SizedFont* Sized = nullptr;

			// Laid out for wrapping (TryGetWrapGlyph).
			bool Wrap = false;

			void Set(const uint8_t* start, int length, const SizedFont* sized, bool wrap, std::shared_ptr<const ShapedRun> shaped);

			[[nodiscard]] bool Overlaps(const uint8_t* start, int length) const { return start < m_end && start + length > m_start; }

			// nullopt if p isn't in the run, or the text there changed.
			std::optional<GameGlyph*> TryGet(const uint8_t* p, ItalicMode italic);

		private:
			[[nodiscard]] bool Matches() const;
		};

		GlyphStore& m_store;
		GameGlyph* m_emptyGlyph;
		ClusterGlyphCache m_cells;
		Shaper m_shaper;
		AgedMap<RunKey, std::shared_ptr<const ShapedRun>> m_runs;

		// Keyed by the largest size's sized font.
		AgedMap<RunKey, std::shared_ptr<const ShapedRun>> m_wrapRuns;

		std::vector<std::unique_ptr<ActiveRun>> m_active;
		std::vector<std::unique_ptr<ActiveRun>> m_freeActive;

	public:
		ShapedRunCache(DirectWriteServices& services, GlyphStore& store, GlyphFactory& factory, const FrameState& state);
		ShapedRunCache(const ShapedRunCache&) = delete;
		ShapedRunCache& operator=(const ShapedRunCache&) = delete;

		// nullptr to have the game look the character up on its own.
		GameGlyph* TryGetGlyph(SizedFont& sized, const uint8_t* p, ItalicMode italic);

		// For wrapping text that may be drawn at any of several sizes: cells of no pixels in the largest size's pixels, each advancing by how
		// much further the furthest of the sizes' pens has got, so that a line measures as wide as at its widest size. sizes and scales go
		// largest last. nullptr as for TryGetGlyph.
		GameGlyph* TryGetWrapGlyph(std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic);

		// A glyph of no pixels that advances by advance, at most 127.
		GameGlyph* GetSpacer(const SizedFont& sized, int advance) { return m_cells.GetSpacer(sized, advance); }

		void Clear();

		// At Present. Runs go first, so that the cells they let go of go too.
		void Sweep();

	private:
		[[nodiscard]] static uint64_t HashBytes(const uint8_t* p, size_t length);

		void ClearActive();

		void Hold(const ShapedRun& run);
		void Drop(const ShapedRun& run);

		// The run's glyph at p, from an active run, else shaped (laid out for wrapping if sizes isn't empty).
		GameGlyph* LookUp(SizedFont& sized, std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic);

		// The run cached for the bytes at p, found without decoding them. nullptr if there is none. Another run whose bytes hash alike is let go
		// of, for this one to take its place.
		std::shared_ptr<const ShapedRun> FindRun(AgedMap<RunKey, std::shared_ptr<const ShapedRun>>& runs, const SizedFont& sized, const uint8_t* p, ItalicMode italic, RunKey& key, int& length);

		std::shared_ptr<const ShapedRun> GetShapedRun(SizedFont& sized, const uint8_t* p, ItalicMode italic, int& length);

		std::shared_ptr<const ShapedRun> GetWrapRun(std::span<SizedFont* const> sizes, std::span<const float> scales, const uint8_t* p, ItalicMode italic, int& length);

		std::shared_ptr<const ShapedRun> Cache(AgedMap<RunKey, std::shared_ptr<const ShapedRun>>& runs, const RunKey& key, std::shared_ptr<const ShapedRun> run);
	};
}
