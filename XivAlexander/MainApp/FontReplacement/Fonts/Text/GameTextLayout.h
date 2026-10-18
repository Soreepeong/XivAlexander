#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/ItalicMode.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Fonts/Records/ShapedCluster.h"
#include "MainApp/FontReplacement/Fonts/Records/TextRun.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class DirectWriteTextAnalyzer;
	class FontServices;
	class IFaceSource;

	// Shapes the game's text (ligatures, contextual alternates, kerning, marks) and answers the game's per-character glyph lookups (LayOutCharacter)
	// from the result: the run a character starts is shaped once, to the line end. Each grapheme goes to the element its font picks; the line is cut
	// into runs of one element, script and direction (DirectWriteTextAnalyzer), each shaped by its source. A shaped cluster is one cell, advancing by
	// the whole-pixel difference of its rounded start and end; right-to-left runs, the game's own glyphs and glyphs a source lacks stay per character.
	// A run ends at an italic macro (ItalicMode). Clusters are placed one after another by their advances on screen (the elements' transformations,
	// emboldening, letter spacing and monospacing), not as shaping laid them out.
	class GameTextLayout {
		static constexpr int MaxRunBytes = 4096;
		static constexpr size_t MaxActiveRuns = 8;
		static constexpr size_t MaxCachedRuns = 8192;
		static constexpr size_t MaxCachedRasters = 8192;

		// Graphemes longer than this are picked an element by their first runes.
		static constexpr int MaxGraphemeRunes = 16;

		// Cells are made at a quarter-pixel resolution of the cluster's position.
		static constexpr float SubpixelSteps = 4;

		using SizedFont = FontReplacer::SizedFont;
		using GlyphSource = FontReplacer::GlyphSource;

		// A cluster's coverage: its font, size and element, and its source's glyphs at its position, compared by value.
		struct RasterKey {
			const MaterializedFont* Font;
			float Px;
			const MaterializedFontElement* Element;
			const IFaceSource* Source;
			float EmSize;
			float Origin;
			int RawAdvance;
			std::vector<uint32_t> Glyphs;
			std::vector<float> Advances;
			std::vector<std::pair<float, float>> Offsets;

			auto operator<=>(const RasterKey&) const = default;
		};

		// A cluster of a line: its glyphs and where they are, and the text it covers.
		struct Cluster {
			// Its glyphs are drawn from it; nullptr if it is left to the per-character path.
			IFaceSource* Source{};

			// Its element's.
			float Px{};

			std::vector<uint32_t> GlyphIds;
			std::vector<float> Advances;
			std::vector<GlyphOffset> Offsets;

			// The advance a monospaced cluster's cell is made from (its glyphs' own, in whole pixels); 0 for the others.
			int RawAdvance{};

			// The kerning (towards the next cluster) its advance has, in pixels on screen.
			float Kerning{};

			// Once made (GetRaster).
			std::optional<RasterKey> Key;

			// The fraction of a pixel the pen is at, in steps of a quarter.
			float Origin{};

			// The whole-pixel pen at the cluster's start and end.
			int X0{};
			int X1{};

			int TextStart{};
			int TextEnd{};

			// Of its first character.
			std::optional<FontReplacer::GlyphSource> GlyphSource;

			// A real italic, which the game must not shear.
			bool RealItalic{};

			// The game draws it, or its source lacks it.
			[[nodiscard]] bool Missing() const { return !Source; }

			// Start moved right by start pixels, end by end; its coverage is made again at its new position.
			[[nodiscard]] Cluster MovedBy(float start, float end) const;
		};

		// The glyph per byte, and whether each character's byte is in italics (nullopt for a run in a node's italics, all of it in them).
		struct ShapedRun {
			std::vector<GameGlyph*> Glyphs;
			std::optional<std::vector<uint8_t>> ItalicBytes;
		};

		// The game reuses its text buffers (each line drawn from one copy buffer, each word measured in another), so a run keeps a copy of its bytes and
		// is used only while the text from its start (which the clusters depend on) to its end is unchanged.
		struct ActiveRun {
			std::vector<uint8_t> Bytes;
			int CompareLength = 0;
			const uint8_t* Start = nullptr;
			const uint8_t* End = nullptr;
			const SizedFont* Sized = nullptr;
			ItalicMode Italic = ItalicMode::Upright;
			std::shared_ptr<const ShapedRun> Shaped;

			void Set(const uint8_t* start, int length, int endBytes, const SizedFont* sized, ItalicMode italic, std::shared_ptr<const ShapedRun> shaped);

			// As the run's italic macros left them, or the node's italics of a run spaced to its upright width.
			[[nodiscard]] ItalicMode ItalicAt(const uint8_t* p) const;

			// Stops at the first difference; the copy has no terminator before its end, so a shorter text never reads past its own.
			[[nodiscard]] bool Matches() const;
		};

		FontReplacer& m_replacer;
		DirectWriteTextAnalyzer& m_analyzer;
		std::map<std::tuple<uint64_t, int, const SizedFont*, ItalicMode>, std::shared_ptr<const ShapedRun>> m_runs;

		// Cells by their sized font too: a cell is placed in its family's atlas, with its metrics, and families may share a font.
		std::map<std::tuple<RasterKey, const SizedFont*, int, int>, GameGlyph*> m_cells;
		std::map<std::pair<const SizedFont*, int>, GameGlyph*> m_spacers;

		std::vector<std::unique_ptr<ActiveRun>> m_active;
		std::vector<std::pair<int, int>> m_absorbed;

		// The clusters of the line being shaped, the analyzer's runs of it, and coverage of clusters by glyphs and position.
		std::vector<Cluster> m_clusters;
		std::vector<TextRun> m_textRuns;
		std::vector<ShapedCluster> m_shaped;
		std::vector<int> m_elementStarts;
		std::map<RasterKey, RasterGlyph> m_rasters;

		// Given to a cluster's characters after the first.
		GameGlyph* m_emptyGlyph;

		// While shaping a run: its text, and each code unit's byte offset, glyph source and whether it is in italics.
		std::vector<wchar_t> m_text = std::vector<wchar_t>(256);
		std::vector<int> m_textToByte = std::vector<int>(256);
		std::vector<std::optional<GlyphSource>> m_textSource = std::vector<std::optional<GlyphSource>>(256);
		std::vector<uint8_t> m_textItalic = std::vector<uint8_t>(256);
		uint8_t m_byteItalic[MaxRunBytes]{};
		const uint8_t* m_shapingBytes = nullptr;
		std::vector<GameGlyph*>* m_shapingGlyphs = nullptr;
		SizedFont* m_shapingSized = nullptr;

	public:
		GameTextLayout(FontServices& services, FontReplacer& replacer, GameGlyph* emptyGlyph);
		GameTextLayout(const GameTextLayout&) = delete;
		GameTextLayout& operator=(const GameTextLayout&) = delete;
		~GameTextLayout();

		// The glyph for the character at p as shaped in its run, in the italics it is in; nullptr to look it up per character.
		GameGlyph* TryGetGlyph(SizedFont& sized, const uint8_t* p, ItalicMode italic);

		// Their glyphs are about to be freed, or their fonts replaced.
		void Clear();

	private:
		// Of the grapheme at t of the text being shaped, which ends at end; in code units.
		[[nodiscard]] int NextGrapheme(int t, int end) const;

		// Uses cached shapings; false if there is nothing to shape.
		bool ShapeFrom(SizedFont& sized, const uint8_t* p, ItalicMode italic, ActiveRun& run);

		void EnsureCapacity(int n);

		// GameTextLayout.Text.cpp

		void Shape(SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic);

		// DirectWrite has no Hangul shaping (no composition, no ljmo/vjmo/tjmo) and draws each jamo full-width; composes L V (T) and LV T as NFC does.
		// The absorbed jamo's bytes are recorded with their syllable's; Old Hangul sequences stay as they are.
		int ComposeHangul(int count);

		// SizedFont::Pick per grapheme (all its code units get it); codepoint replacements go in where the UTF-16 length allows.
		void AssignSources(int count);

		// GameTextLayout.Clusters.cpp

		// Code units in m_textItalic are shaped with their sources' italics; with italicImages, each single glyph shaped upright is then drawn
		// with its font's italic, where there is one.
		void Collect(int count, bool italicImages);

		// Of one glyph source and script run, at pen on; those its source lacks, the game draws or right-to-left ones are left per character.
		void CollectPiece(const std::optional<GlyphSource>& glyphSource, int start, int end, const TextRun& run, bool inItalic, bool italicImages, float& pen);

		// Advances pen by its advance on screen: with letter spacing, or its monospaced cell (made now, as its advance is). Kerning is what shaping
		// moved the glyphs by besides their own advances; a monospaced cluster that drops it (xivres wrapping_fixed_size_font) drops the one before's too.
		void AddCluster(const GlyphSource& glyphSource, IFaceSource* source, float px, const ShapedCluster& shaped, int textOffset, bool realItalic, bool italicImages, float& pen);

		// On screen, at their own advances, without what shaping moved them by.
		[[nodiscard]] static float GetOwnWidth(IFaceSource& source, float px, std::span<const uint32_t> glyphs, std::span<const GlyphOffset> offsets);

		// At the pen, which they don't advance.
		void AddMissing(int start, int end, float pen);

		// A run in a text node's italics: shaped in italics, spaced out evenly to end where the upright shaping does (the game measures it upright).
		// false, collecting nothing, if the shapings' clusters differ or one is left to the game.
		bool CollectSpreadItalics(int count);

		// GameTextLayout.Cells.cpp

		// The game has no left bearing, so ink left of a cluster's pen starts its cell earlier (up to the previous cell's start), and the previous cell
		// advances that much less; the line keeps its width and the ink lands where shaping put it.
		void PlaceClusters();

		GameGlyph* GetSpacer(int advance);

		// Relative to the pen: as the source draws it (monospaced from its own advance), adjusted by its element (MaterializedFontElement::Wrap) and
		// finished (FinishGlyph). A monospaced cluster's advance is its cell's; the others' is set when their cells are made.
		const RasterGlyph& GetRaster(Cluster& c);

		// The box starts pad pixels left of the pen.
		GameGlyph* GetCell(Cluster& c, int pad, int advance);
	};
}
