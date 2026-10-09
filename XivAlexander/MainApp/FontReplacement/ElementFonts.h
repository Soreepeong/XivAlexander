#pragma once

#include <FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h>

#include "MainApp/FontReplacement/FreeTypeFonts.h"
#include "MainApp/FontReplacement/LookupFont.h"

struct FT_FaceRec_;

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct GameFont;

	// In whole pixels.
	struct LineMetrics {
		int Ascent = 0;
		int LineHeight = 0;
	};

	// A face element's glyphs (xivres fixed_size_font), gamma applied but before ReplacementFace::Wrap. Sizes are the element's
	// (the face's size scaled by the element's); glyphs are relative to the pen on the baseline.
	class IElementFont {
	public:
		virtual ~IElementFont() = default;

		// The game's glyphs are looked up in game.
		virtual bool Has(char32_t codepoint, GameFont* game) = 0;

		virtual LineMetrics GetLineMetrics(float px, GameFont* game) = 0;

		// BASE table baseline by tag (as DWRITE_FONT_FEATURE_TAG), in pixels above the glyphs' origin.
		virtual std::optional<float> GetBaseline(uint32_t tag, float px) = 0;

		// nullopt if there is none or the game draws it.
		virtual std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) = 0;

		// For monospacing: resampled, unless the font can draw it narrower.
		virtual RasterGlyph Squeeze(char32_t codepoint, float px, const RasterGlyph& glyph, float scale) { return glyph.SqueezedX(scale); }

		// On screen, unrounded (monospacing's reference glyph); 0 if none.
		virtual float GetAdvance(char32_t codepoint, float px) = 0;
	};

	// xivres fontdata_fixed_size_font; the game draws these itself.
	class GameElementFont final : public IElementFont {
	public:
		bool Has(char32_t codepoint, GameFont* game) override;
		LineMetrics GetLineMetrics(float px, GameFont* game) override;
		std::optional<float> GetBaseline(uint32_t tag, float px) override { return std::nullopt; }
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override { return std::nullopt; }
		float GetAdvance(char32_t codepoint, float px) override { return 0; }
	};

	// xivres empty_fixed_size_font; metrics in pixels at the element's size.
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

	// xivres directwrite/freetype_fixed_size_font: FreeType if the element says so or the font has only bitmaps, else
	// DirectWrite; shaped with DirectWrite in layouts set up by ApplyTo.
	class OutlineElementFont final : public IElementFont {
		GlyphRasterizer& m_rasterizer;
		const FontChanger::Structs::FaceElement& m_def;
		std::unique_ptr<LookupFont> m_font;
		FT_FaceRec_* m_freeTypeFace;
		std::optional<std::vector<uint8_t>> m_gamma;
		GlyphTransform m_transform;
		// The faces are kept so that their addresses aren't reused.
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

		// renderer is for synthesis; nullptr if the family isn't installed.
		static std::unique_ptr<OutlineElementFont> Create(GlyphRasterizer& rasterizer, const FontChanger::Structs::FaceElement& def, FontChanger::Structs::RendererEnum renderer, std::optional<std::vector<uint8_t>> gamma, const std::string& faceName);

		[[nodiscard]] LookupFont& Font() const { return *m_font; }

		// Layouts ask for it (with its language and features).
		[[nodiscard]] const FontChanger::Structs::LookupStruct& Lookup() const { return m_def.Lookup; }

		// On screen: synthesis first, then the element's.
		[[nodiscard]] const GlyphTransform& Transform() const { return m_transform; }

		// The face's, which its text is laid out with.
		DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_NATURAL;

		[[nodiscard]] const std::set<char32_t>& AllCodepoints();

		bool Has(char32_t codepoint, GameFont* game) override;
		LineMetrics GetLineMetrics(float px, GameFont* game) override { return GetLineMetrics(px, m_transform); }

		// Scaled vertically as glyphs transformed by transform are.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, const GlyphTransform& transform) const;

		std::optional<float> GetBaseline(uint32_t tag, float px) override;
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override;

		// Rasterized at the scale, which keeps hinting and stroke weight.
		RasterGlyph Squeeze(char32_t codepoint, float px, const RasterGlyph& glyph, float scale) override;

		float GetAdvance(char32_t codepoint, float px) override;

		// FreeType's emboldening, which DirectWrite's layout doesn't include.
		[[nodiscard]] float GetExtraAdvance(float emSize) const;

		// FreeType if the font is drawn so and the face is its own, else DirectWrite; transformed (by the element unless
		// transform is given), then squeezed by squeezeX. originX is on screen, after the transformation.
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

		// Vertical ink extent in pixels, with its shaped-run offset, as RasterizeRun draws it; nullopt if it has no ink.
		[[nodiscard]] std::optional<std::pair<int, int>> MeasureGlyph(IDWriteFontFace* face, float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const GlyphTransform& transform);

		// Horizontal, in pixels from the origin, from the outline's side bearings.
		[[nodiscard]] static std::pair<float, float> GetInkExtent(IDWriteFontFace* face, float size, uint16_t glyph, const GlyphTransform& transform);

		// Sets font, size, language, features and axis values, asking for what matches the font with its simulations.
		void ApplyTo(IDWriteTextLayout* layout, DWRITE_TEXT_RANGE range, float emSize);

		// The font's own face (with simulations and axis values at the size) if the layout picked the font, else runFace.
		[[nodiscard]] IDWriteFontFace* GetRunFace(IDWriteFontFace* runFace, float emSize);

	private:
		// advance includes emboldening and is unscaled; false if none.
		bool TryGetGlyph(char32_t codepoint, float px, IDWriteFontFace*& face, uint16_t& index, float& advance);

		// nullptr if the font sets no OpenType features.
		IDWriteTypography* GetTypography();

		// Same file and index, whatever the simulations and axes; files are compared as a family's styles often share glyph counts.
		bool SameFont(IDWriteFontFace* a, IDWriteFontFace* b);
	};

	// xivres image_fixed_size_font: SVG/PNG files per glyph as FontChanger's GlyphFiles makes them; one font per size.
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

		[[nodiscard]] const std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>& GetFont(float px, const GlyphTransform& transform);
	};

	// xivres glyph_merging_fixed_size_font: each merged codepoint is a text in a shape, drawn with textOutline (owned unless
	// it's the base font) or the base glyph images. One font per size, pixel values scaled from the element's size.
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

		// Its own and its texts'.
		[[nodiscard]] std::vector<OutlineElementFont*> OutlineFonts() const;

		bool Has(char32_t codepoint, GameFont* game) override { return m_codepoints.contains(codepoint); }
		LineMetrics GetLineMetrics(float px, GameFont* game) override { return m_baseFont->GetLineMetrics(px, game); }
		std::optional<float> GetBaseline(uint32_t tag, float px) override { return m_baseFont->GetBaseline(tag, px); }
		std::optional<RasterGlyph> Rasterize(char32_t codepoint, float px) override;

		// Monospacing's reference glyph is measured in the base font.
		float GetAdvance(char32_t codepoint, float px) override { return m_baseFont->GetAdvance(codepoint, px); }
	};

	// Relative to the pen on the baseline; nullopt if it has none.
	[[nodiscard]] std::optional<RasterGlyph> RasterizeFixedSizeFontGlyph(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint);
}
