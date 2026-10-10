#pragma once

#include "MainApp/FontReplacement/ReplacementFace.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;
	struct GameGlyph;
	struct SizedFont;

	enum class ItalicMode {
		Upright,

		// Italic macro (the game sees it when measuring): shaped in the faces' italics; the game shears faces without one.
		Real,

		// Text node italics (the game measures upright): shaped as Real, spread evenly to the upright width; where the two
		// shapings differ, shaped upright with each single-character cluster drawn in its font's italic.
		Images,
	};

	// Grapheme cluster length in code units; 0 for empty text.
	[[nodiscard]] int GetNextTextElementLength(std::wstring_view text);

	// Answers the game's per-character glyph lookup (LayOutCharacter) from a DirectWrite shaping of the run from that character
	// to the line end, macros skipped.
	// Each cluster becomes one cell at its subpixel origin on its first character (others get a zero-width glyph); RTL runs
	// and missing glyphs stay per character. Text is laid out several times a frame, so runs are cached by content and size.
	class TextShaper final {
	public:
		// Private-use characters go to it before the system fallback, which would pick Windows' Segoe Fluent Icons.
		static constexpr auto IconFamily = "XIV AXIS Std ATK";

	private:
		static constexpr int MaxRunBytes = 4096;

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

		struct Cluster {
			IDWriteFontFace* Face{};
			float EmSize{};
			std::vector<uint16_t> Glyphs;
			std::vector<float> Advances;
			std::optional<std::vector<DWRITE_GLYPH_OFFSET>> Offsets;

			// Set by GetRaster.
			std::optional<RasterKey> Key;

			// Subpixel pen fraction, in quarter-pixel steps.
			float Origin{};

			// Whole-pixel pen at the cluster's start and end.
			int X0{};
			int X1{};

			// Coverage in its cell (ReplacementFace::Wrap); made during layout if the element is monospaced.
			std::optional<RasterGlyph> Cell;

			int TextStart{};
			int TextEnd{};

			// Of the first character; nullptr for the system's fonts.
			const FaceElement* Element{};

			// A glyph is missing or the game draws it: the cluster is left per character.
			bool Missing{};

			// Something not shaped here (a right-to-left run) comes right before it.
			bool AfterGap{};

			// A face's real italics, which the game must not shear.
			bool RealItalic{};

			[[nodiscard]] Cluster MovedBy(float start, float end) const;
		};

		// Glyph per byte; ItalicBytes is nullopt for a run in a node's italics (all of it italic).
		struct ShapedRun {
			std::vector<GameGlyph*> Glyphs;
			std::optional<std::vector<uint8_t>> ItalicBytes;
		};

		// The game reuses its text buffers, so a run keeps a copy of its bytes and is used only while the text from the lookup to its end is unchanged.
		struct ActiveRun {
			std::vector<uint8_t> Bytes;
			int CompareLength = 0;
			const uint8_t* Start = nullptr;
			const uint8_t* End = nullptr;
			const SizedFont* Sized = nullptr;
			ItalicMode Italic = ItalicMode::Upright;
			std::shared_ptr<const ShapedRun> Shaped;

			void Set(const uint8_t* start, int length, int endBytes, const SizedFont* sized, ItalicMode italic, std::shared_ptr<const ShapedRun> shaped);

			// As the run's italic macros left them, or the node's italics for a run spread to its upright width.
			[[nodiscard]] ItalicMode ItalicAt(const uint8_t* p) const;

			// Stops at the first difference; the copy has no terminator before its end, so a shorter text never reads past its own.
			[[nodiscard]] bool Matches(const uint8_t* p) const;
		};

		GlyphRasterizer& m_rasterizer;
		FontReplacer& m_replacer;
		GlyphRunCollector m_collector;
		std::map<std::pair<const ReplacementFace*, int>, IDWriteTextFormat1Ptr> m_formats;
		std::map<std::tuple<uint64_t, int, const SizedFont*, ItalicMode>, std::shared_ptr<const ShapedRun>> m_runs;

		// Face -> face without its synthesized oblique; face -> its family's italic face (nullptr for none).
		std::map<IDWriteFontFace*, IDWriteFontFace*> m_uprightFaces;
		std::map<IDWriteFontFace*, IDWriteFontFace*> m_italicFaces;
		std::map<std::tuple<RasterKey, int, int>, GameGlyph*> m_cells;
		std::map<std::tuple<const ReplacementFace*, float, int>, GameGlyph*> m_spacers;
		std::map<IDWriteFontFace*, IDWriteFontFacePtr> m_keptFaces;
		std::vector<std::unique_ptr<ActiveRun>> m_active;
		std::vector<std::pair<int, int>> m_absorbed;

		std::vector<Cluster> m_clusters;
		std::map<RasterKey, RasterGlyph> m_rasters;
		bool m_clusterGap = false;

		// Private-use characters go to the icon font first; faces without system fallback use GlyphRasterizer::NoFallback.
		IDWriteFontFallbackPtr m_fallback;
		GameGlyph* m_emptyGlyph = nullptr;

		// The run being shaped, per code unit.
		std::vector<wchar_t> m_text = std::vector<wchar_t>(256);
		std::vector<int> m_textToByte = std::vector<int>(256);
		std::vector<const FaceElement*> m_textElement = std::vector<const FaceElement*>(256);
		std::vector<uint8_t> m_textItalic = std::vector<uint8_t>(256);
		uint8_t m_byteItalic[MaxRunBytes]{};
		const uint8_t* m_shapingBytes = nullptr;
		std::vector<GameGlyph*>* m_shapingGlyphs = nullptr;
		const SizedFont* m_shapingSized = nullptr;
		ItalicMode m_shapingItalic = ItalicMode::Upright;

		// Pen drift from the layout, from emboldening, transformations, monospacing and letter spacing DirectWrite doesn't know of.
		float m_penShift = 0;

	public:
		TextShaper(GlyphRasterizer& rasterizer, FontReplacer& replacer);
		TextShaper(const TextShaper&) = delete;
		TextShaper& operator=(const TextShaper&) = delete;
		~TextShaper();

		// The formats depend on the faces; call before they change.
		void ClearFormats();

		// nullptr to look the glyph up per character; emptyGlyph is the zero-width glyph for a cluster's later characters.
		GameGlyph* TryGetGlyph(const SizedFont& sized, const uint8_t* p, GameGlyph* emptyGlyph, ItalicMode italic);

		// Call before their glyphs are freed.
		void Clear();

		// [i] is set if a cluster ends / a line may break (UAX #14, dictionary for Thai) after i code units; both text.size()+1 long.
		void GetBreaks(std::wstring_view text, std::span<uint8_t> clusterEnd, std::span<uint8_t> wrapAfter);

	private:
		void OnGlyphRun(float baselineX, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description);

		// A real italic or oblique style, not a synthesized one.
		static bool IsRealItalic(IDWriteFontFace* face);

		// The same glyphs without the synthesized oblique; the face itself if it has none.
		IDWriteFontFace* GetUprightFace(IDWriteFontFace* face);

		// By family name, weight and stretch among system fonts, as faces may come from files with simulations or axis values.
		IDWriteFontFace* GetItalicFace(IDWriteFontFace* face);

		// So that its address isn't reused.
		IDWriteFontFace* Keep(IDWriteFontFace* face);

		// The game has no left bearing, so ink left of the pen starts the cell earlier (up to the previous cell's start) and
		// shortens the previous cell's advance by as much.
		void PlaceClusters();

		GameGlyph* GetSpacer(int advance);

		static IDWriteFontFallbackPtr CreateSystemFallback(GlyphRasterizer& rasterizer);

		// Uses cached shapings; false if there is nothing to shape.
		bool ShapeFrom(const SizedFont& sized, const uint8_t* p, ItalicMode italic, ActiveRun& run);

		void EnsureCapacity(int n);

		void Shape(const SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic);

		// With ItalicMode::Real, code units in m_textItalic are laid out in the faces' italics, which the fallback picks too.
		void Collect(const SizedFont& sized, int count, ItalicMode italic);

		// Shaped in italics but spread to end where the upright shaping does (the game measures node italics upright); false,
		// collecting nothing, if the shapings' clusters differ or one is left to the game.
		bool CollectSpreadItalics(const SizedFont& sized, int count);

		// DirectWrite has no Hangul shaping (no composition, no ljmo/vjmo/tjmo) and draws each jamo full-width; composes as NFC does.
		int ComposeHangul(int count);

		// A low surrogate gets its high one's element; codepoint replacements are put in where the UTF-16 length allows.
		void AssignElements(const SizedFont& sized, int count);

		// Text no element draws keeps the format's font: the face's first, with the system fallback if the face has it.
		void ApplyElements(IDWriteTextLayout* layout, const SizedFont& sized, int count);

		// The face's first font (Segoe UI if none) at that font's size, with the system fallback or none as the face says.
		IDWriteTextFormat1* GetFormat(const ReplacementFace& face, float px);

		// Relative to the pen, after ReplacementFace::Wrap and FinishGlyph; the advance is set when its cell is made.
		const RasterGlyph& GetRaster(Cluster& c);

		RasterGlyph RasterizeCluster(const Cluster& c, int advance, float squeezeX);

		// The box starts pad pixels left of the pen.
		GameGlyph* GetCell(Cluster& c, int pad, int advance);
	};
}
