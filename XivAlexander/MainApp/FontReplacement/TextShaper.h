#pragma once

#include "MainApp/FontReplacement/ReplacementFace.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;
	struct GameGlyph;
	struct SizedFont;

	// How a run of text the game draws in italics is shaped.
	enum class ItalicMode {
		// Not in italics.
		Upright,

		// In italics by an italic macro, which the game also sees when it measures: shaped in the faces' italics, which are
		// drawn as they are; the game shears the glyphs of faces without (synthesized obliques are drawn upright for it).
		Real,

		// In italics by the text node, which the game only sees when it draws, measuring the text upright: shaped in italics as
		// Real, spaced out evenly to the upright run's width. Where the two shapings differ, shaped upright instead, each
		// single-character cluster drawn with the italic of its font, where there is one.
		Images,
	};

	// Gets the length of the text element (grapheme cluster) at the start of text, in code units; 0 for empty text.
	[[nodiscard]] int GetNextTextElementLength(std::wstring_view text);

	// Shapes the game's text with DirectWrite (ligatures, contextual alternates, kerning, marks, fallback per script), and
	// answers the game's per-character glyph lookups from the result.
	//
	// The game lays text out one character at a time: its text walkers (FUN_140652BE0, FUN_140653030, and AtkFontAnalyzerBase
	// vf4 for text inserted by macros) call FUN_1406EEC70 for each character, which looks the glyph up and advances the pen by
	// its width. With the character's address known, the run it starts or continues is shaped once: from there to the end of
	// the line, macro payloads skipped (a colour change doesn't break a ligature), as UTF-16 for DirectWrite.
	//
	// Each shaped cluster (a ligature, a base and its marks, a conjunct) becomes one cell: its glyphs rasterized together at
	// the fraction of a pixel shaping put it at, advancing by the whole-pixel difference of its rounded start and end, so a
	// line of clusters ends where shaping ends it. The first character of a cluster gets the cell, the others a glyph of no
	// width. Right-to-left runs and glyphs a font lacks are left to the per-character path.
	//
	// Text is laid out every frame (several times: measuring, counting, drawing), so shaped runs are kept by content and size,
	// and the last few runs by address, used while the text at the address is unchanged.
	//
	// A run ends at an italic macro, and is shaped as the italics it is in says (ItalicMode). The cells of real italic glyphs
	// are told to the replacer, which keeps the game from shearing them.
	class TextShaper final {
	public:
		// The game's icon font: private-use characters go to it before the system fallback, which would pick an icon font of
		// Windows' own (Segoe Fluent Icons) for them.
		static constexpr auto IconFamily = "XIV AXIS Std ATK";

	private:
		static constexpr int MaxRunBytes = 4096;

		// The key of a cluster's coverage: its face, size and element, and its glyphs at its position.
		struct RasterKey {
			const ReplacementFace* Face;
			float Px;
			const FaceElement* Element;
			IDWriteFontFace* FontFace;
			float EmSize;
			float Origin;
			std::vector<uint16_t> Glyphs;
			std::vector<float> Advances;
			bool HasOffsets;
			std::vector<std::pair<float, float>> Offsets;

			auto operator<=>(const RasterKey&) const = default;
		};

		// A cluster of a layout: its glyphs and where they are, and the text it covers.
		struct Cluster {
			IDWriteFontFace* Face{};
			float EmSize{};
			std::vector<uint16_t> Glyphs;
			std::vector<float> Advances;
			std::optional<std::vector<DWRITE_GLYPH_OFFSET>> Offsets;

			// The key of the cluster's coverage, once made (GetRaster).
			std::optional<RasterKey> Key;

			// The fraction of a pixel the pen is at, in steps of a quarter.
			float Origin{};

			// The whole-pixel pen at the cluster's start and end.
			int X0{};
			int X1{};

			// The cluster's coverage in its cell (ReplacementFace::Wrap), made as it is laid out if its element is monospaced.
			std::optional<RasterGlyph> Cell;

			int TextStart{};
			int TextEnd{};

			// The face element of the cluster's first character, or nullptr for the system's fonts.
			const FaceElement* Element{};

			// Whether a glyph is missing from the font, or the game draws it (the cluster is left per character).
			bool Missing{};

			// Whether something not shaped here (a right-to-left run) comes right before it.
			bool AfterGap{};

			// Whether its glyphs are a face's real italics, which the game must not shear.
			bool RealItalic{};

			// Gets the cluster moved right: its start by start pixels, its end by end.
			[[nodiscard]] Cluster MovedBy(float start, float end) const;
		};

		// A shaped run: the glyph for each byte, and whether each character's byte is in italics (none for a run in a node's
		// italics, all of it in them).
		struct ShapedRun {
			std::vector<GameGlyph*> Glyphs;
			std::optional<std::vector<uint8_t>> ItalicBytes;
		};

		// A run shaped at an address. The game reuses its buffers (each line drawn from one copy buffer, each word measured in
		// another), so an address says nothing about the text there now: the run keeps a copy of its bytes, and a lookup in it
		// only uses it while the text from the looked-up character to the run's end is still the same.
		struct ActiveRun {
			std::vector<uint8_t> Bytes;
			int CompareLength = 0;
			const uint8_t* Start = nullptr;
			const uint8_t* End = nullptr;
			const SizedFont* Sized = nullptr;
			ItalicMode Italic = ItalicMode::Upright;
			std::shared_ptr<const ShapedRun> Shaped;

			void Set(const uint8_t* start, int length, int endBytes, const SizedFont* sized, ItalicMode italic, std::shared_ptr<const ShapedRun> shaped);

			// Gets the italics the character at p (inside the run) was shaped in: as the run's italic macros left them, or the
			// node's italics of a run spaced to its upright width.
			[[nodiscard]] ItalicMode ItalicAt(const uint8_t* p) const;

			// Gets whether the text from p (inside the run) to its end is what was shaped. Compared byte by byte, stopping at
			// the first difference: the copy has no terminator before its end, so a shorter text stops it before reading past
			// its own terminator.
			[[nodiscard]] bool Matches(const uint8_t* p) const;
		};

		GlyphRasterizer& m_rasterizer;
		FontReplacer& m_replacer;
		GlyphRunCollector m_collector;
		std::map<std::pair<const ReplacementFace*, int>, IDWriteTextFormat1Ptr> m_formats;
		std::map<std::tuple<uint64_t, int, const SizedFont*, ItalicMode>, std::shared_ptr<const ShapedRun>> m_runs;

		// Faces without their synthesized oblique, by face; and the italic face of a face's family (nullptr for none), by face.
		std::map<IDWriteFontFace*, IDWriteFontFace*> m_uprightFaces;
		std::map<IDWriteFontFace*, IDWriteFontFace*> m_italicFaces;
		std::map<std::tuple<RasterKey, int, int>, GameGlyph*> m_cells;
		std::map<std::tuple<const ReplacementFace*, float, int>, GameGlyph*> m_spacers;
		std::map<IDWriteFontFace*, IDWriteFontFacePtr> m_keptFaces;
		std::vector<std::unique_ptr<ActiveRun>> m_active;
		std::vector<std::pair<int, int>> m_absorbed;

		// The clusters of the layout being shaped, and coverage of clusters by glyphs and position.
		std::vector<Cluster> m_clusters;
		std::map<RasterKey, RasterGlyph> m_rasters;
		bool m_clusterGap = false;

		// The system's fallback, private-use characters to the game's icon font first. A face without system fallback lays out
		// each character in the font of its element only (GlyphRasterizer::NoFallback).
		IDWriteFontFallbackPtr m_fallback;
		GameGlyph* m_emptyGlyph = nullptr;

		// While shaping a run: its text, and each code unit's byte offset, element and whether it is in italics.
		std::vector<wchar_t> m_text = std::vector<wchar_t>(256);
		std::vector<int> m_textToByte = std::vector<int>(256);
		std::vector<const FaceElement*> m_textElement = std::vector<const FaceElement*>(256);
		std::vector<uint8_t> m_textItalic = std::vector<uint8_t>(256);
		uint8_t m_byteItalic[MaxRunBytes]{};
		const uint8_t* m_shapingBytes = nullptr;
		std::vector<GameGlyph*>* m_shapingGlyphs = nullptr;
		const SizedFont* m_shapingSized = nullptr;
		ItalicMode m_shapingItalic = ItalicMode::Upright;

		// How far the pen has moved from where the layout put it: by the difference of each cell's advance and the layout's, as
		// elements' emboldening, transformations, monospacing and letter spacing make it, which DirectWrite doesn't know of.
		float m_penShift = 0;

	public:
		TextShaper(GlyphRasterizer& rasterizer, FontReplacer& replacer);
		TextShaper(const TextShaper&) = delete;
		TextShaper& operator=(const TextShaper&) = delete;
		~TextShaper();

		// Releases the text formats, which depend on the faces (before the faces change).
		void ClearFormats();

		// Gets the glyph for the character at p as shaped in its run, in the italics it is in, or nullptr to look it up per
		// character. emptyGlyph is the glyph of no width given to a cluster's characters after the first.
		GameGlyph* TryGetGlyph(const SizedFont& sized, const uint8_t* p, GameGlyph* emptyGlyph, ItalicMode italic);

		// Forgets every shaped run and cell (their glyphs are about to be freed).
		void Clear();

		// Gets where a text may be split: clusterEnd[i] is set when a cluster ends after the first i code units, and
		// wrapAfter[i] when a line may also break there (UAX #14, dictionary breaks for Thai and the like). Both are one longer
		// than text.
		void GetBreaks(std::wstring_view text, std::span<uint8_t> clusterEnd, std::span<uint8_t> wrapAfter);

	private:
		void OnGlyphRun(float baselineX, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description);

		// Gets whether a face is a real italic (or oblique) style of its family, not one synthesized.
		static bool IsRealItalic(IDWriteFontFace* face);

		// Gets a face without its synthesized oblique (the same glyphs, upright), or the face if it has none.
		IDWriteFontFace* GetUprightFace(IDWriteFontFace* face);

		// Gets the italic face of a face's family among the system's fonts (by its family name, weight and stretch, as faces
		// may be made from their files, with simulations or axis values), or nullptr if the family has none of its own.
		IDWriteFontFace* GetItalicFace(IDWriteFontFace* face);

		// Keeps a face for as long as the shaper, so that its address stays its own.
		IDWriteFontFace* Keep(IDWriteFontFace* face);

		// Makes the cells of the clusters a layout drew, in order. The game has no left bearing: a cell's box starts at its
		// pen, so ink left of a cluster's pen (a j's hook, a kerned pair's overhang) would be moved right. Instead the cell
		// starts that much earlier, up to the previous cell's start, and the previous cell advances that much less; the line's
		// width stays the same, and the ink lands where shaping put it.
		void PlaceClusters();

		// Gets a glyph that draws nothing and advances by advance.
		GameGlyph* GetSpacer(int advance);

		// Makes the system's font fallback, with private-use characters going to the game's icon font first.
		static IDWriteFontFallbackPtr CreateSystemFallback(GlyphRasterizer& rasterizer);

		// Shapes (or finds shaped) the run starting at p into run; false if there is nothing to shape.
		bool ShapeFrom(const SizedFont& sized, const uint8_t* p, ItalicMode italic, ActiveRun& run);

		void EnsureCapacity(int n);

		void Shape(const SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic);

		// Lays the text being shaped out, and collects its clusters as italic says (OnGlyphRun). With ItalicMode::Real, the
		// code units in italics (m_textItalic) are laid out in the faces' italics, which the system's fallback picks too.
		void Collect(const SizedFont& sized, int count, ItalicMode italic);

		// Collects a run in a text node's italics: shaped in italics, at the italics' own positions, but spaced out evenly to
		// end where the run shaped upright does (the game measures it upright, as it doesn't see the node's italics then).
		// False, with nothing collected, if the two shapings don't make the same clusters (a ligature only one has), or a
		// cluster is left to the game: then each character is drawn in italics at its upright place instead.
		bool CollectSpreadItalics(const SizedFont& sized, int count);

		// Composes conjoining jamo (L V, L V T, and a precomposed LV syllable followed by T) into precomposed syllables, as NFC
		// does: DirectWrite has no Hangul shaping (it applies neither composition nor the fonts' ljmo/vjmo/tjmo), so it would
		// draw each jamo as a separate full-width letter. The absorbed jamo's bytes are recorded with their syllable's. Old
		// Hangul sequences, which have no precomposed syllable, are left as they are.
		int ComposeHangul(int count);

		// Finds the face element of each code unit of the text (a low surrogate gets its high one's), and puts in the
		// characters an element draws with another's glyph (its codepoint replacements), where the UTF-16 length allows.
		void AssignElements(const SizedFont& sized, int count);

		// Sets each element's font, size and features on the text it draws. Text no element draws keeps the format's: the
		// face's first font, with the system's fallback if the face has it (and dropped otherwise).
		void ApplyElements(IDWriteTextLayout* layout, const SizedFont& sized, int count);

		// Gets the text format of a face at a size: its first font (Segoe UI if none) at that font's size, with the system's
		// fallback or none as the face says.
		IDWriteTextFormat1* GetFormat(const ReplacementFace& face, float px);

		// Gets a cluster's coverage, relative to its pen, adjusted as its element says (ReplacementFace::Wrap) and finished
		// (FinishGlyph). Its advance is set when its cell is made.
		const RasterGlyph& GetRaster(Cluster& c);

		// Rasterizes a cluster as its element draws it, squeezed horizontally by squeezeX.
		RasterGlyph RasterizeCluster(const Cluster& c, int advance, float squeezeX);

		// Gets the cell of a cluster whose box starts pad pixels left of its pen and advances by advance.
		GameGlyph* GetCell(Cluster& c, int pad, int advance);
	};
}
