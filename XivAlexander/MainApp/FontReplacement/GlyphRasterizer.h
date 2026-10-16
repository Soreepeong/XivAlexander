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

	// Left to right, or top to bottom for the vertical ones (a panel turned to portrait, or one made so and turned to landscape); None draws grayscale.
	enum class SubpixelOrder : uint8_t {
		None,
		Rgb,
		Bgr,
		VerticalRgb,
		VerticalBgr,
	};

	[[nodiscard]] constexpr bool IsVertical(SubpixelOrder order) { return order == SubpixelOrder::VerticalRgb || order == SubpixelOrder::VerticalBgr; }

	// R, G, B coverage is stored swapped from the stripes' order.
	[[nodiscard]] constexpr bool IsBlueFirst(SubpixelOrder order) { return order == SubpixelOrder::Bgr || order == SubpixelOrder::VerticalBgr; }

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

		// Empty, or R, G, B subpixel coverage per pixel of Alpha (which is their mean); resampling drops it.
		std::vector<uint8_t> Lcd;

		[[nodiscard]] bool HasLcd() const { return !Lcd.empty(); }

		// Scaled about the pen, shifted right and down by a fraction of a pixel.
		static void ScaledBounds(int left, int top, int width, int height, float scale, float shiftX, float shiftY, int& x0, int& y0, int& w, int& h);

		// About the pen, shifted by a fraction of a pixel; shrunk, a pixel averages the source area it covers, enlarged, 4 x 4 bilinear samples.
		[[nodiscard]] RasterGlyph Scaled(float scale, float shiftX, float shiftY) const;

		// About the pen; each pixel averages the source columns it covers.
		[[nodiscard]] RasterGlyph SqueezedX(float scale) const;

		[[nodiscard]] RasterGlyph Trimmed() const;

		// The pieces share one pen; overlaps keep the larger coverage.
		static RasterGlyph Merge(std::vector<RasterGlyph> pieces, int advance);

		static void BlitMax(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);

		// Into 3 bytes a pixel; g's Alpha stands in for all three without Lcd.
		static void BlitMaxLcd(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y);

		// Maps coverage to coverage^exponent.
		static std::vector<uint8_t> CoverageTable(float exponent);

		[[nodiscard]] RasterGlyph WithCoverage(const std::vector<uint8_t>& table) const;

	private:
		// Per destination pixel from first, the source pixels it covers and by how much (summing to 1 / scale).
		static std::vector<std::vector<std::pair<int, float>>> AreaWeights(int first, int count, float origin, float scale, int sourceSize);

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
		DWRITE_RENDERING_MODE1 RenderMode = DWRITE_RENDERING_MODE1_NATURAL;
		DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
		DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;

		static DirectWriteParams Of(const FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct& c) {
			return {c.RenderMode, c.MeasureMode, c.GridFitMode};
		}
	};

	class GlyphRasterizer {
		IDWriteFactory3Ptr m_factory;
		IDWriteFontCollectionPtr m_systemFonts;
		IDWriteFontFallbackPtr m_noFallback;
		std::unique_ptr<FreeTypeFonts> m_freeType;
		GlyphRunCollector m_runCollector;
		SubpixelOrder m_subpixelOrder = SubpixelOrder::None;
		bool m_verticalWithFreeType = true;

	public:
		GlyphRasterizer();
		GlyphRasterizer(const GlyphRasterizer&) = delete;
		GlyphRasterizer& operator=(const GlyphRasterizer&) = delete;
		~GlyphRasterizer();

		[[nodiscard]] IDWriteFactory2* Factory() const { return m_factory; }

		// nullptr if FreeType can't be used.
		[[nodiscard]] FreeTypeFonts* FreeType() const { return m_freeType.get(); }

		// Text is laid out in its own font only.
		[[nodiscard]] IDWriteFontFallback* NoFallback() const { return m_noFallback; }

		[[nodiscard]] GlyphRunCollector& RunCollector() { return m_runCollector; }

		// FreeType's too. Game thread, before glyphs are made again.
		[[nodiscard]] SubpixelOrder GetSubpixelOrder() const { return m_subpixelOrder; }
		void SetSubpixelOrder(SubpixelOrder order);

		// For vertical orders, which DirectWrite has no ClearType for: runs are drawn with FreeType's LCD_V rendering; else (or if FreeType can't open
		// the font) DirectWrite draws them turned a quarter, unhinted. Game thread, before glyphs are made again.
		[[nodiscard]] bool GetVerticalWithFreeType() const { return m_verticalWithFreeType; }
		void SetVerticalWithFreeType(bool value) { m_verticalWithFreeType = value; }

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

	private:
		// As RasterizeRun with FreeType: the face's file, its simulations made as FreeType makes them, its axis values, and hinted as the parameters'
		// rendering and grid fitting modes are; nullopt if FreeType can't open it, or it has only bitmaps.
		[[nodiscard]] std::optional<RasterGlyph> RasterizeWithFreeType(
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
			float originY) const;

		// turned draws the run turned a quarter, its screen's (x, y) at (y, -x), so ClearType's horizontal stripes are vertical when turned back
		// (TurnBack): antialiased both ways and unhinted, as both are then needed across the text.
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
			const GlyphTransform& transform,
			DWRITE_TEXT_ANTIALIAS_MODE antialiasMode,
			bool turned) const;

		// Pixels of a run drawn turned, width by height with channels bytes each, turned back: rows become columns, the last on the left. A pixel's
		// channels stay in order, now top to bottom.
		[[nodiscard]] static std::vector<uint8_t> TurnBack(const std::vector<uint8_t>& pixels, int width, int height, int channels);

		// The box of a run drawn turned, turned back.
		[[nodiscard]] static RECT TurnBack(const RECT& turned);
	};
}
