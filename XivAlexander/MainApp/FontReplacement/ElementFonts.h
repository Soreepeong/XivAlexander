#pragma once

#include <FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h>

#include "MainApp/FontReplacement/FreeTypeFonts.h"
#include "MainApp/FontReplacement/LookupFont.h"

struct FT_FaceRec_;

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;

	// Line metrics in whole pixels.
	struct LineMetrics {
		int Ascent = 0;
		int LineHeight = 0;
	};

	// The glyphs a face element draws (xivres fixed_size_font), at any size: what it has, its line metrics and baselines, and
	// its glyphs, rasterized as the element draws them (its gamma applied) but not yet adjusted by the face
	// (ReplacementFace::Wrap). Made by ReplacementFace from an element's renderer: the game's glyphs, none, a font
	// (DirectWrite or FreeType), glyph images, or glyph merging around one of those.
	//
	// Sizes are the element's (the face's drawn size scaled by the element's size); glyphs are placed relative to the pen on
	// the baseline.
	class IElementFont {
	public:
		virtual ~IElementFont() = default;

		// Gets whether a codepoint has a glyph; the game's glyphs are those of game.
		virtual bool Has(char32_t codepoint, GameFont* game) = 0;

		// Gets the line metrics at a size, in whole pixels.
		virtual LineMetrics GetLineMetrics(float px, GameFont* game) = 0;

		// Gets a baseline of the BASE table (by tag, as DWRITE_FONT_FEATURE_TAG) in pixels above the glyphs' origin.
		virtual std::optional<float> GetBaseline(uint32_t tag, float px) = 0;

		// Rasterizes a codepoint's glyph at a size; nullopt if there is none, or the game draws it.
		virtual std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) = 0;

		// Gets a glyph (rasterized by Rasterize) drawn narrower by a horizontal scale, for monospacing: resampled, unless the
		// font can draw it so.
		virtual RasterGlyph Squeeze(char32_t codepoint, float px, const RasterGlyph& glyph, float scale) { return glyph.SqueezedX(scale); }

		// Gets a codepoint's advance on screen at a size, unrounded (monospacing's reference glyph); 0 if none.
		virtual float GetAdvance(char32_t codepoint, float px) = 0;
	};

	// The game's own glyphs (xivres fontdata_fixed_size_font), which the game draws.
	class GameElementFont final : public IElementFont {
	public:
		bool Has(char32_t codepoint, GameFont* game) override;
		LineMetrics GetLineMetrics(float px, GameFont* game) override;
		std::optional<float> GetBaseline(uint32_t tag, float px) override { return std::nullopt; }
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override { return std::nullopt; }
		float GetAdvance(char32_t codepoint, float px) override { return 0; }
	};

	// No glyphs, only line metrics (xivres empty_fixed_size_font), given in pixels at the element's size.
	class EmptyElementFont final : public IElementFont {
		float m_size;
		float m_ascent;
		float m_lineHeight;

	public:
		EmptyElementFont(float size, float ascent, float lineHeight)
			: m_size(size), m_ascent(ascent), m_lineHeight(lineHeight) {}

		bool Has(char32_t codepoint, GameFont* game) override { return false; }
		LineMetrics GetLineMetrics(float px, GameFont* game) override;
		std::optional<float> GetBaseline(uint32_t tag, float px) override { return std::nullopt; }
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override { return std::nullopt; }
		float GetAdvance(char32_t codepoint, float px) override { return 0; }
	};

	// A font's glyphs (xivres directwrite_fixed_size_font and freetype_fixed_size_font): with FreeType if the element is drawn
	// so or its font has only bitmaps, else with DirectWrite. Its glyphs are also shaped with DirectWrite, in text laid out
	// with ApplyTo.
	class OutlineElementFont final : public IElementFont {
		GlyphRasterizer& m_rasterizer;
		const FontChanger::Structs::FaceElement& m_def;
		std::unique_ptr<LookupFont> m_font;
		FT_FaceRec_* m_freeTypeFace;
		std::optional<std::vector<uint8_t>> m_gamma;
		GlyphTransform m_transform;
		// Whether two faces are of the same font file, by the faces (kept, so that their addresses stay theirs).
		struct SameFontEntry {
			IDWriteFontFacePtr A;
			IDWriteFontFacePtr B;
			bool Same;
		};
		std::map<std::pair<IDWriteFontFace*, IDWriteFontFace*>, SameFontEntry> m_sameFonts;
		std::optional<std::map<uint32_t, int>> m_baselines;
		IDWriteTypographyPtr m_typography;
		bool m_typographyMade = false;
		std::optional<std::set<char32_t>> m_codepoints;

		OutlineElementFont(GlyphRasterizer& rasterizer, const FontChanger::Structs::FaceElement& def, std::unique_ptr<LookupFont> font, FT_FaceRec_* freeTypeFace, std::optional<std::vector<uint8_t>> gamma);

	public:

		// Finds the font an element's lookup names and makes it as it draws (renderer, for synthesis); nullptr if the family
		// isn't installed.
		static std::unique_ptr<OutlineElementFont> Create(GlyphRasterizer& rasterizer, const FontChanger::Structs::FaceElement& def, FontChanger::Structs::RendererEnum renderer, std::optional<std::vector<uint8_t>> gamma, const std::string& faceName);

		// Gets the font.
		[[nodiscard]] LookupFont& Font() const { return *m_font; }

		// Gets the lookup the font was found by, which layouts ask for (with its language and features).
		[[nodiscard]] const FontChanger::Structs::LookupStruct& Lookup() const { return m_def.Lookup; }

		// Gets how the glyphs are transformed on screen: by synthesis, then by the element.
		[[nodiscard]] const GlyphTransform& Transform() const { return m_transform; }

		// How glyphs are measured: the face's, which its text is laid out with.
		DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_NATURAL;

		// Gets every codepoint the font has.
		[[nodiscard]] const std::set<char32_t>& AllCodepoints();

		bool Has(char32_t codepoint, GameFont* game) override;
		LineMetrics GetLineMetrics(float px, GameFont* game) override { return GetLineMetrics(px, m_transform); }

		// Gets the line metrics at a size, scaled vertically as glyphs transformed by transform are.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, const GlyphTransform& transform) const;

		std::optional<float> GetBaseline(uint32_t tag, float px) override;
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override;

		// Draws the glyph narrower: rasterized at the scale, which keeps its hinting and stroke weight.
		RasterGlyph Squeeze(char32_t codepoint, float px, const RasterGlyph& glyph, float scale) override;

		float GetAdvance(char32_t codepoint, float px) override;

		// Gets how much further glyphs advance than DirectWrite lays them out: FreeType's emboldening.
		[[nodiscard]] float GetExtraAdvance(float emSize) const;

		// Rasterizes a run of glyphs of a font face (one glyph, or a shaped cluster): with FreeType if the font is drawn so and
		// the face is its own, else with DirectWrite. The glyphs are transformed as the element says (or by transform), then
		// squeezed horizontally by squeezeX; originX is on screen, after the transformation, and advance is the advance the
		// glyph is given.
		[[nodiscard]] RasterGlyph RasterizeRun(
			IDWriteFontFace* face,
			float size,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			float squeezeX = 1,
			const GlyphTransform* transform = nullptr,
			float originY = 0);

		// Gets the vertical extent in pixels of a glyph's ink, moved by its offset in a shaped run, as RasterizeRun would draw
		// it; nullopt if it has none.
		[[nodiscard]] std::optional<std::pair<int, int>> MeasureGlyph(IDWriteFontFace* face, float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const GlyphTransform& transform);

		// Gets a glyph's ink extent horizontally in pixels from its origin, from its outline's side bearings.
		[[nodiscard]] static std::pair<float, float> GetInkExtent(IDWriteFontFace* face, float size, uint16_t glyph, const GlyphTransform& transform);

		// Sets the font, size, language, features and axis values on a range of a text layout, asking for what matches the
		// font with its simulations.
		void ApplyTo(IDWriteTextLayout* layout, DWRITE_TEXT_RANGE range, float emSize);

		// Gets the face a glyph run of a layout is drawn from: the font's own (with its simulations and axis values at the
		// size) if the layout picked the font, else the layout's.
		[[nodiscard]] IDWriteFontFace* GetRunFace(IDWriteFontFace* runFace, float emSize);

	private:
		// Gets a codepoint's glyph in the face at a size and its advance (with emboldening; unscaled); false if none.
		bool TryGetGlyph(char32_t codepoint, float px, IDWriteFontFace*& face, uint16_t& index, float& advance);

		// Gets the font's OpenType features as a typography; nullptr if it sets none.
		IDWriteTypography* GetTypography();

		// Gets whether two faces are of the same font (the same file and index, so the same glyphs), whatever their
		// simulations and axis values. A family's styles often have the same glyph count, so the files are compared; the
		// answer is kept, with a reference to the faces so their pointers aren't reused.
		bool SameFont(IDWriteFontFace* a, IDWriteFontFace* b);
	};

	// Glyph images (xivres image_fixed_size_font): SVG and PNG files per glyph, as FontChanger's GlyphFiles makes them, one
	// font per size.
	class ImageElementFont final : public IElementFont {
		FontChanger::Structs::GlyphImagesStruct m_settings;
		float m_gamma;
		GlyphTransform m_transform;
		std::set<char32_t> m_codepoints;
		std::map<std::tuple<float, float, float, float, float>, std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>> m_fonts;

	public:
		ImageElementFont(FontChanger::Structs::GlyphImagesStruct settings, float gamma, const GlyphTransform& transform);

		bool Has(char32_t codepoint, GameFont* game) override { return m_codepoints.contains(codepoint); }
		LineMetrics GetLineMetrics(float px, GameFont* game) override;
		std::optional<float> GetBaseline(uint32_t tag, float px) override { return std::nullopt; }
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override;
		float GetAdvance(char32_t codepoint, float px) override;

		// Gets the font of the glyphs at a size, transformed by transform.
		[[nodiscard]] const std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>& GetFont(float px, const GlyphTransform& transform);
	};

	// Glyph merging (xivres glyph_merging_fixed_size_font) around an element's font: its merged codepoints only, each a text
	// put in a shape, the text drawn with fonts made at a size and horizontal scale (from textOutline, which it owns, if it
	// isn't the base font; else with the base glyph images). Metrics and baselines are the font's. One merging font per size,
	// with the merging's pixel values scaled from the element's size to it.
	class MergingElementFont final : public IElementFont {
		GlyphRasterizer& m_rasterizer;
		std::unique_ptr<IElementFont> m_baseFont;
		const FontChanger::Structs::FaceElement& m_def;
		std::unique_ptr<OutlineElementFont> m_ownTextOutline;
		OutlineElementFont* m_textOutline;
		std::set<char32_t> m_codepoints;
		std::map<float, std::shared_ptr<FontChanger::FixedSizeFont::glyph_merging_fixed_size_font>> m_fonts;

	public:
		MergingElementFont(GlyphRasterizer& rasterizer, std::unique_ptr<IElementFont> baseFont, const FontChanger::Structs::FaceElement& def, std::unique_ptr<OutlineElementFont> ownTextOutline);

		// Gets the fonts of DirectWrite and FreeType it draws with: its own, and its texts'.
		[[nodiscard]] std::vector<OutlineElementFont*> OutlineFonts() const;

		bool Has(char32_t codepoint, GameFont* game) override { return m_codepoints.contains(codepoint); }
		LineMetrics GetLineMetrics(float px, GameFont* game) override { return m_baseFont->GetLineMetrics(px, game); }
		std::optional<float> GetBaseline(uint32_t tag, float px) override { return m_baseFont->GetBaseline(tag, px); }
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override;

		// Gets the advance of the font's own glyph (monospacing's reference glyph is measured in it).
		float GetAdvance(char32_t codepoint, float px) override { return m_baseFont->GetAdvance(codepoint, px); }
	};

	// Draws a glyph of a fixed size font into coverage relative to the pen on the baseline; nullopt if it has none.
	[[nodiscard]] std::optional<RasterGlyph> RasterizeFixedSizeFontGlyph(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint);
}
