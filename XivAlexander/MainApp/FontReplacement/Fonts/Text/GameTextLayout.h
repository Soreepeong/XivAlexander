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

	// The game lays text out a character at a time (LayOutCharacter), so a character no active run has is shaped with the rest of its line,
	// which later lookups are answered from.
	class GameTextLayout {
		static constexpr int MaxRunBytes = 4096;
		static constexpr size_t MaxActiveRuns = 8;
		static constexpr size_t MaxCachedRuns = 8192;
		static constexpr size_t MaxCachedRasters = 8192;

		// Graphemes longer than this are picked an element by their first runes.
		static constexpr int MaxGraphemeRunes = 16;

		// Cells are made at a quarter-pixel resolution of the cluster's position.
		static constexpr float SubpixelSteps = 4;

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

		struct Cluster {
			// nullptr if the cluster is left to the per-character path.
			IFaceSource* Source{};

			// The element's size the source draws at.
			float Px{};

			std::vector<uint32_t> GlyphIds;
			std::vector<float> Advances;
			std::vector<GlyphOffset> Offsets;

			// A monospaced cluster's own advance in whole pixels, which its cell is made from; 0 for the others.
			int RawAdvance{};

			// In pixels on screen, towards the next cluster.
			float Kerning{};

			std::optional<RasterKey> Key;

			// The fraction of a pixel the pen is at, in SubpixelSteps.
			float Origin{};

			// Whole-pixel pen positions.
			int X0{};
			int X1{};

			int TextStart{};
			int TextEnd{};

			// Of the cluster's first character.
			std::optional<FontReplacement::GlyphSource> GlyphSource;

			// A real italic, which the game must not shear.
			bool RealItalic{};

			[[nodiscard]] bool Missing() const { return !Source; }

			[[nodiscard]] Cluster MovedBy(float start, float end) const;
		};

		// ItalicBytes is nullopt for a run in a node's italics, which is all in them.
		struct ShapedRun {
			std::vector<GameGlyph*> Glyphs;
			std::optional<std::vector<uint8_t>> ItalicBytes;
		};

		// A run shaped at an address, used only while the text there is unchanged since shaping, as the game reuses its text buffers.
		struct ActiveRun {
			std::vector<uint8_t> Bytes;
			int CompareLength = 0;
			const uint8_t* Start = nullptr;
			const uint8_t* End = nullptr;
			const SizedFont* Sized = nullptr;
			ItalicMode Italic = ItalicMode::Upright;
			std::shared_ptr<const ShapedRun> Shaped;

			void Set(const uint8_t* start, int length, const SizedFont* sized, ItalicMode italic, std::shared_ptr<const ShapedRun> shaped);

			[[nodiscard]] ItalicMode ItalicAt(const uint8_t* p) const;

			[[nodiscard]] bool Matches() const;
		};

		FontReplacer& m_replacer;
		DirectWriteTextAnalyzer& m_analyzer;
		GameGlyph* m_emptyGlyph;
		std::map<std::tuple<uint64_t, int, const SizedFont*, ItalicMode>, std::shared_ptr<const ShapedRun>> m_runs;

		// Keyed by sized font too, as families may share a font but each places cells in its own atlas with its own metrics.
		std::map<std::tuple<RasterKey, const SizedFont*, int, int>, GameGlyph*> m_cells;
		std::map<std::pair<const SizedFont*, int>, GameGlyph*> m_spacers;
		std::map<RasterKey, RasterGlyph> m_rasters;

		std::vector<std::unique_ptr<ActiveRun>> m_active;
		std::vector<std::pair<int, int>> m_absorbed;
		std::vector<Cluster> m_clusters;
		std::vector<TextRun> m_textRuns;
		std::vector<ShapedCluster> m_shaped;
		std::vector<int> m_elementStarts;

		// Per UTF-16 code unit of the run being shaped; a byte offset of -1 (a low surrogate or replaced character) gets no glyph.
		std::vector<wchar_t> m_text = std::vector<wchar_t>(256);
		std::vector<int> m_textToByte = std::vector<int>(256);
		std::vector<std::optional<GlyphSource>> m_textSource = std::vector<std::optional<GlyphSource>>(256);
		std::vector<uint8_t> m_textItalic = std::vector<uint8_t>(256);
		uint8_t m_byteItalic[MaxRunBytes]{};
		SizedFont* m_shapingSized = nullptr;

	public:
		GameTextLayout(FontServices& services, FontReplacer& replacer, GameGlyph* emptyGlyph);
		GameTextLayout(const GameTextLayout&) = delete;
		GameTextLayout& operator=(const GameTextLayout&) = delete;

		// nullptr to have the game look the character up on its own.
		GameGlyph* TryGetGlyph(SizedFont& sized, const uint8_t* p, ItalicMode italic);

		void Clear();

	private:
		[[nodiscard]] int NextGrapheme(int t, int end) const;

		// The length is in code units; nullopt for a lone surrogate.
		[[nodiscard]] std::optional<std::pair<char32_t, int>> DecodeAt(int u, int end) const;

		std::shared_ptr<const ShapedRun> GetShapedRun(SizedFont& sized, const uint8_t* p, ItalicMode italic, int& length);

		void EnsureCapacity(int n);

		// Returns the run's count of UTF-16 code units, and its length in bytes in length.
		int ReadRun(const uint8_t* p, ItalicMode italic, int& length);

		void Shape(SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic);

		// Composes conjoining jamo into precomposed syllables as NFC does, since DirectWrite has no Hangul shaping and would draw each jamo as a
		// separate full-width letter; Old Hangul, having no precomposed syllables, is left as it is.
		int ComposeHangul(int count);

		void AssignSources(int count);

		void Collect(int count, bool italicImages);

		void CollectPiece(const std::optional<GlyphSource>& glyphSource, int start, int end, const TextRun& run, bool inItalic, bool italicImages, float& pen);

		void AddCluster(const GlyphSource& glyphSource, IFaceSource* source, float px, const ShapedCluster& shaped, int textOffset, bool realItalic, bool italicImages, float& pen);

		[[nodiscard]] static float GetOwnWidth(IFaceSource& source, float px, std::span<const uint32_t> glyphs, std::span<const GlyphOffset> offsets);

		void AddMissing(int start, int end, float pen);

		// The game measures a node's italics upright, so the italic shaping is spread out to end where the upright one does; false, with nothing
		// collected, if the two shapings' clusters differ or any is left to the game.
		bool CollectSpreadItalics(int count);

		// The game has no left bearing, so a cell with ink left of its pen starts that much earlier (but not before the previous cell) and the
		// previous cell advances that much less, keeping the line's width and the ink where shaping put it.
		void PlaceClusters(const uint8_t* bytes, std::vector<GameGlyph*>& glyphs);

		GameGlyph* GetSpacer(int advance);

		// Relative to the pen; the advance is final only for a monospaced cluster, as GetCell sets the others'.
		const RasterGlyph& GetRaster(Cluster& c);

		// The cell's box starts pad pixels left of the cluster's pen.
		GameGlyph* GetCell(Cluster& c, const uint8_t* bytes, int pad, int advance);
	};
}
