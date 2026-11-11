#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/Fonts/Text/ShapingContext.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class CellCache;
	class DirectWriteTextAnalyzer;

	// Shapes a run of the game's text into a glyph per byte.
	class Shaper {
	public:
		// The advance Measure gives a byte left to the game.
		static constexpr int Missing = -1;

	private:
		// Graphemes longer than this are picked an element by their first runes.
		static constexpr int MaxGraphemeRunes = 16;

		DirectWriteTextAnalyzer& m_analyzer;
		CellCache& m_cells;
		GameGlyph* m_emptyGlyph;
		ShapingContext m_ctx;

		// RasterizeConcurrently's, reused.
		std::vector<Cluster*> m_pending;
		std::set<RasterKey> m_seen;

	public:
		Shaper(DirectWriteTextAnalyzer& analyzer, CellCache& cells, GameGlyph* emptyGlyph)
			: m_analyzer(analyzer)
			, m_cells(cells)
			, m_emptyGlyph(emptyGlyph) {}

		Shaper(const Shaper&) = delete;
		Shaper& operator=(const Shaper&) = delete;

		// Per byte of the run last read, whether an italic macro has it in italics.
		[[nodiscard]] std::span<const uint8_t> ByteItalic() const { return m_ctx.ByteItalic; }

		// Finds the run at p, and if decode, reads it for Shape. Returns the run's count of UTF-16 code units if decoded, and its length in bytes
		// in length.
		int ReadRun(const uint8_t* p, ItalicMode italic, bool decode, int& length);

		// Shapes the run ReadRun read, of count code units.
		void Shape(SizedFont& sized, const uint8_t* p, int count, std::span<GameGlyph*> glyphs, ItalicMode italic);

		// How far each byte of the run ReadRun read advances, as Shape would place it, without rasterizing. Missing marks a byte left to the
		// game.
		void Measure(SizedFont& sized, int count, std::span<int> advances, ItalicMode italic);

	private:
		void ClusterRun(SizedFont& sized, int count, ItalicMode italic);

		// Composes conjoining jamo into precomposed syllables as NFC does, since DirectWrite has no Hangul shaping and would draw each jamo as a
		// separate full-width letter. Old Hangul, having no precomposed syllables, is left as it is.
		int ComposeHangul(int count);

		void AssignSources(int count);

		[[nodiscard]] static float GetOwnWidth(IFaceSource& source, float px, std::span<const uint32_t> glyphs, std::span<const GlyphOffset> offsets);

		void AddMissing(int start, int end, float pen);

		void Collect(int count, bool italicImages);

		void CollectPiece(const std::optional<GlyphSource>& glyphSource, int start, int end, const TextRun& run, bool inItalic, bool italicImages, float& pen);

		void AddCluster(const GlyphSource& glyphSource, IFaceSource* source, float px, const ShapedCluster& shaped, int textOffset, bool realItalic, bool italicImages, float& pen);

		// Collects a run in a node's italics shaped in italics but spread out to end where it does upright, as the game measures it upright.
		// False, with nothing collected, if the two shapings' clusters differ or any is left to the game.
		bool CollectSpreadItalics(int count);

		// The game has no left bearing, so a cell with ink left of its pen starts that much earlier (but not before the previous cell) and the
		// previous cell advances that much less, keeping the line's width and the ink where shaping put it.
		void PlaceClusters(const uint8_t* bytes, std::span<GameGlyph*> glyphs);

		// As PlaceClusters advances, but from the clusters' pens, as cells' left bearings don't change a line's width.
		void MeasureClusters(std::span<int> advances);

		// The clusters not rasterized yet whose sources allow it are rasterized on the thread pool together, which this thread waits for, so
		// that new text costs about as long as its slowest cluster.
		void RasterizeConcurrently();
	};
}
