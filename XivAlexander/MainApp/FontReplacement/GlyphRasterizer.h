#pragma once

#include <dwrite_3.h>

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/GlyphRunCollector.h"

_COM_SMARTPTR_TYPEDEF(IDWriteTextFormat1, __uuidof(IDWriteTextFormat1));
_COM_SMARTPTR_TYPEDEF(IDWriteTextLayout1, __uuidof(IDWriteTextLayout1));
_COM_SMARTPTR_TYPEDEF(IDWriteTextLayout4, __uuidof(IDWriteTextLayout4));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalFontFileLoader, __uuidof(IDWriteLocalFontFileLoader));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FreeTypeFonts;

	// A linear transformation of glyphs on screen, where y grows downwards: x' = M11 x + M12 y, y' = M21 x + M22 y
	// (FontChanger's screen matrix). Advances scale by M11, and line metrics by |M22|.
	struct GlyphTransform {
		float M11 = 1;
		float M12 = 0;
		float M21 = 0;
		float M22 = 1;

		static constexpr GlyphTransform Identity() { return {}; }

		// Makes one of FontChanger's transformation on screen.
		static GlyphTransform Of(const FontChanger::FixedSizeFont::font_render_transformation_matrix& m) { return {m.M11, m.M12, m.M21, m.M22}; }
		static GlyphTransform Of(const FontChanger::Structs::TransformStruct& t) { return Of(t.GetScreenMatrix()); }

		[[nodiscard]] FontChanger::FixedSizeFont::font_render_transformation_matrix ToMatrix() const { return {M11, M12, M21, M22}; }

		// Gets the transformation that applies first, then this one.
		[[nodiscard]] GlyphTransform After(const GlyphTransform& first) const {
			return {
				M11 * first.M11 + M12 * first.M21,
				M11 * first.M12 + M12 * first.M22,
				M21 * first.M11 + M22 * first.M21,
				M21 * first.M12 + M22 * first.M22,
			};
		}

		// Gets the transformation that applies this one, and then scales horizontally by x.
		[[nodiscard]] GlyphTransform ScaledX(float x) const { return {M11 * x, M12 * x, M21, M22}; }

		bool operator==(const GlyphTransform&) const = default;
	};

	// A rasterized glyph: 8-bit coverage, placed relative to the pen on the baseline.
	struct RasterGlyph {
		int Advance = 0;
		int Left = 0;
		int Top = 0;
		int Width = 0;
		int Height = 0;
		std::vector<uint8_t> Alpha;

		// Gets the box a glyph's coverage takes when scaled by scale about the pen, moved right and down by a fraction of a
		// pixel.
		static void ScaledBounds(int left, int top, int width, int height, float scale, float shiftX, float shiftY, int& x0, int& y0, int& w, int& h);

		// Gets the glyph scaled by scale about the pen, moved right and down by a fraction of a pixel: each output pixel
		// averages 4 x 4 bilinear samples, so it both enlarges and shrinks.
		[[nodiscard]] RasterGlyph Scaled(float scale, float shiftX, float shiftY) const;

		// Gets the glyph scaled horizontally by scale about the pen, each pixel averaging the source columns it covers.
		[[nodiscard]] RasterGlyph SqueezedX(float scale) const;

		// Gets the glyph with its empty rows and columns cut off.
		[[nodiscard]] RasterGlyph Trimmed() const;

		// Merges glyphs placed relative to one pen into one box with an advance, keeping the larger coverage where they
		// overlap.
		static RasterGlyph Merge(std::vector<RasterGlyph> pieces, int advance);

		// Draws a glyph's coverage into a buffer at a position, keeping the larger value where it overlaps.
		static void BlitMax(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);

		// Makes a table mapping coverage to coverage to the power of exponent.
		static std::vector<uint8_t> CoverageTable(float exponent);

		// Gets the glyph with its coverage mapped through a table.
		[[nodiscard]] RasterGlyph WithCoverage(const std::vector<uint8_t>& table) const;

	private:
		[[nodiscard]] float Bilinear(float u, float v) const;
		[[nodiscard]] float At(int x, int y) const;
	};

	// The (first) file of a font face: its loader and reference key, which tell its file apart from others'. Loader is null
	// if the face has no file.
	struct FontFileKey {
		IDWriteFontFilePtr File;
		IDWriteFontFileLoaderPtr Loader;
		const void* Key = nullptr;
		uint32_t KeySize = 0;

		static FontFileKey Of(IDWriteFontFace* face);

		[[nodiscard]] bool SameFileAs(const FontFileKey& other) const {
			return Loader && Loader == other.Loader && KeySize == other.KeySize && std::memcmp(Key, other.Key, KeySize) == 0;
		}
	};

	// DirectWrite rendering parameters of an element (values of DWRITE_RENDERING_MODE, _MEASURING_MODE, _GRID_FIT_MODE).
	struct DirectWriteParams {
		DWRITE_RENDERING_MODE RenderMode = DWRITE_RENDERING_MODE_NATURAL;
		DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
		DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;

		static DirectWriteParams Of(const FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct& c) {
			return {c.RenderMode, c.MeasureMode, c.GridFitMode};
		}
	};

	// Finds system fonts and rasterizes glyph runs with DirectWrite, grayscale antialiased.
	class GlyphRasterizer {
		IDWriteFactory2Ptr m_factory;
		IDWriteFontCollectionPtr m_systemFonts;
		IDWriteFontFallbackPtr m_noFallback;
		std::unique_ptr<FreeTypeFonts> m_freeType;
		GlyphRunCollector m_runCollector;

	public:
		GlyphRasterizer();
		GlyphRasterizer(const GlyphRasterizer&) = delete;
		GlyphRasterizer& operator=(const GlyphRasterizer&) = delete;
		~GlyphRasterizer();

		// Gets the DirectWrite factory.
		[[nodiscard]] IDWriteFactory2* Factory() const { return m_factory; }

		// Gets the system's font collection.
		[[nodiscard]] IDWriteFontCollection* SystemFonts() const { return m_systemFonts; }

		// Gets FreeType, or nullptr if it can't be used.
		[[nodiscard]] FreeTypeFonts* FreeType() const { return m_freeType.get(); }

		// Gets a font fallback that falls back to nothing: text is laid out in its own font only.
		[[nodiscard]] IDWriteFontFallback* NoFallback() const { return m_noFallback; }

		// Gets a collector of text layouts' glyph runs.
		[[nodiscard]] GlyphRunCollector& RunCollector() { return m_runCollector; }

		// Finds the font of a family closest to a weight, stretch and style (simulated if the family lacks it); nullptr if
		// the family isn't installed.
		[[nodiscard]] IDWriteFontPtr FindFont(const std::string& name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style) const;

		// Makes a text format that doesn't wrap, of a family at a weight, style and stretch and a size, with a font fallback.
		[[nodiscard]] IDWriteTextFormat1Ptr CreateFormat(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, float size, IDWriteFontFallback* fallback) const;

		// Rasterizes a run of glyphs of one face (a glyph, or a shaped cluster: a ligature, a base with its marks) with the
		// pen at originX, a fraction of a pixel, so the cluster lands where shaping put it; and with the baseline at originY
		// (glyph merging's texts are placed so).
		[[nodiscard]] RasterGlyph RasterizeRun(
			IDWriteFontFace* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const DirectWriteParams& parameters,
			const GlyphTransform& transform,
			float originY = 0) const;

		// Gets the pixels a run of glyphs covers with the pen at (originX, originY), as RasterizeRun draws it.
		[[nodiscard]] RECT GetRunBounds(
			IDWriteFontFace* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			const DirectWriteParams& parameters,
			const GlyphTransform& transform,
			float originX = 0,
			float originY = 0) const;

	private:
		// Makes DirectWrite's analysis of a glyph run.
		[[nodiscard]] IDWriteGlyphRunAnalysisPtr CreateAnalysis(
			IDWriteFontFace* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			float originY,
			const DirectWriteParams& parameters,
			const GlyphTransform& transform) const;
	};
}
