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

	// On screen, y down: x' = M11 x + M12 y, y' = M21 x + M22 y (FontChanger's screen matrix); advances scale by M11, line metrics by |M22|.
	struct GlyphTransform {
		float M11 = 1;
		float M12 = 0;
		float M21 = 0;
		float M22 = 1;

		static constexpr GlyphTransform Identity() { return {}; }

		static GlyphTransform Of(const FontChanger::FixedSizeFont::font_render_transformation_matrix& m) { return {m.M11, m.M12, m.M21, m.M22}; }
		static GlyphTransform Of(const FontChanger::Structs::TransformStruct& t) { return Of(t.GetScreenMatrix()); }

		[[nodiscard]] FontChanger::FixedSizeFont::font_render_transformation_matrix ToMatrix() const { return {M11, M12, M21, M22}; }

		// Applies first, then this one.
		[[nodiscard]] GlyphTransform After(const GlyphTransform& first) const {
			return {
				M11 * first.M11 + M12 * first.M21,
				M11 * first.M12 + M12 * first.M22,
				M21 * first.M11 + M22 * first.M21,
				M21 * first.M12 + M22 * first.M22,
			};
		}

		// This one, then a horizontal scale by x.
		[[nodiscard]] GlyphTransform ScaledX(float x) const { return {M11 * x, M12 * x, M21, M22}; }

		bool operator==(const GlyphTransform&) const = default;
	};

	// 8-bit coverage, relative to the pen on the baseline.
	struct RasterGlyph {
		int Advance = 0;
		int Left = 0;
		int Top = 0;
		int Width = 0;
		int Height = 0;
		std::vector<uint8_t> Alpha;

		// Scaled about the pen, shifted right and down by a fraction of a pixel.
		static void ScaledBounds(int left, int top, int width, int height, float scale, float shiftX, float shiftY, int& x0, int& y0, int& w, int& h);

		// About the pen, shifted by a fraction of a pixel; each pixel averages 4 x 4 bilinear samples, so it both enlarges and shrinks.
		[[nodiscard]] RasterGlyph Scaled(float scale, float shiftX, float shiftY) const;

		// About the pen; each pixel averages the source columns it covers.
		[[nodiscard]] RasterGlyph SqueezedX(float scale) const;

		[[nodiscard]] RasterGlyph Trimmed() const;

		// The pieces share one pen; overlaps keep the larger coverage.
		static RasterGlyph Merge(std::vector<RasterGlyph> pieces, int advance);

		// Keeps the larger value where it overlaps.
		static void BlitMax(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);

		// Maps coverage to coverage^exponent.
		static std::vector<uint8_t> CoverageTable(float exponent);

		[[nodiscard]] RasterGlyph WithCoverage(const std::vector<uint8_t>& table) const;

	private:
		[[nodiscard]] float Bilinear(float u, float v) const;
		[[nodiscard]] float At(int x, int y) const;
	};

	// The face's first file; loader and key tell files apart. Loader is null if the face has no file.
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

	struct DirectWriteParams {
		DWRITE_RENDERING_MODE RenderMode = DWRITE_RENDERING_MODE_NATURAL;
		DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
		DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;

		static DirectWriteParams Of(const FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct& c) {
			return {c.RenderMode, c.MeasureMode, c.GridFitMode};
		}
	};

	// Rasterizes with DirectWrite, grayscale antialiased.
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

		[[nodiscard]] IDWriteFactory2* Factory() const { return m_factory; }

		[[nodiscard]] IDWriteFontCollection* SystemFonts() const { return m_systemFonts; }

		// nullptr if FreeType can't be used.
		[[nodiscard]] FreeTypeFonts* FreeType() const { return m_freeType.get(); }

		// Text is laid out in its own font only.
		[[nodiscard]] IDWriteFontFallback* NoFallback() const { return m_noFallback; }

		[[nodiscard]] GlyphRunCollector& RunCollector() { return m_runCollector; }

		// Closest match, simulated if the family lacks the style; nullptr if the family isn't installed.
		[[nodiscard]] IDWriteFontPtr FindFont(const std::string& name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style) const;

		// Doesn't wrap.
		[[nodiscard]] IDWriteTextFormat1Ptr CreateFormat(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, float size, IDWriteFontFallback* fallback) const;

		// originX is the pen's subpixel fraction, so a cluster lands where shaping put it; originY places the baseline (glyph merging).
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

		// As RasterizeRun draws it.
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
