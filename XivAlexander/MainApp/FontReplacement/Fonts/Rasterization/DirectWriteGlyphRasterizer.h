#pragma once

#include <FontChanger.FixedSizeFont/directwrite_fixed_size_font.h>

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Utilities/Matrix2x2.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FreeTypeGlyphRasterizer;

	struct DirectWriteParams {
		DWRITE_RENDERING_MODE1 RenderMode = DWRITE_RENDERING_MODE1_NATURAL;
		DWRITE_MEASURING_MODE MeasureMode = DWRITE_MEASURING_MODE_GDI_CLASSIC;
		DWRITE_GRID_FIT_MODE GridFitMode = DWRITE_GRID_FIT_MODE_ENABLED;

		static DirectWriteParams Of(const FontChanger::FixedSizeFont::directwrite_fixed_size_font::create_struct& c) {
			return {c.RenderMode, c.MeasureMode, c.GridFitMode};
		}
	};

	// Grayscale antialiased, with ClearType coverage too for a subpixel order (vertical ones drawn turned a quarter, or with FreeType's LCD_V).
	class DirectWriteGlyphRasterizer {
		IDWriteFactory7* m_factory;
		FreeTypeGlyphRasterizer& m_freeType;
		SubpixelOrder m_subpixelOrder = SubpixelOrder::None;
		bool m_verticalWithFreeType = true;

	public:
		DirectWriteGlyphRasterizer(IDWriteFactory7* factory, FreeTypeGlyphRasterizer& freeType)
			: m_factory(factory)
			, m_freeType(freeType) {}

		DirectWriteGlyphRasterizer(const DirectWriteGlyphRasterizer&) = delete;
		DirectWriteGlyphRasterizer& operator=(const DirectWriteGlyphRasterizer&) = delete;

		// Whether RasterizeRun gives ClearType coverage too, in which order. Game thread, before glyphs are made again.
		[[nodiscard]] SubpixelOrder GetSubpixelOrder() const { return m_subpixelOrder; }
		void SetSubpixelOrder(SubpixelOrder order) { m_subpixelOrder = order; }

		// For vertical orders, which DirectWrite has no ClearType for: runs are drawn with FreeType's LCD_V rendering; else (or if FreeType can't open
		// the font) DirectWrite draws them turned a quarter, unhinted. Game thread, before glyphs are made again.
		[[nodiscard]] bool VerticalWithFreeType() const { return m_verticalWithFreeType; }
		void SetVerticalWithFreeType(bool value) { m_verticalWithFreeType = value; }

		// One face's glyph or shaped cluster; originX is the pen's subpixel fraction, so a cluster lands where shaping put it. originY places the
		// baseline (glyph merging).
		[[nodiscard]] RasterGlyph RasterizeRun(
			IDWriteFontFace5* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const DirectWriteParams& parameters,
			const Matrix2x2& transform,
			float originY = 0) const;

		// The pixels RasterizeRun would cover with the pen at the origin.
		[[nodiscard]] RECT GetRunBounds(
			IDWriteFontFace5* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			const DirectWriteParams& parameters,
			const Matrix2x2& transform) const;

	private:
		// Aliased runs have no subpixel coverage.
		[[nodiscard]] bool UsesSubpixel(const DirectWriteParams& parameters) const;

		// As RasterizeRun with FreeType: the face's file, its simulations made as FreeType makes them, its axis values, and hinted as the parameters'
		// rendering and grid fitting modes are; nullopt if FreeType can't open it, or it has only bitmaps.
		[[nodiscard]] std::optional<RasterGlyph> RasterizeWithFreeType(
			IDWriteFontFace5* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const DirectWriteParams& parameters,
			const Matrix2x2& transform,
			float originY) const;

		// nullopt if the face isn't variable.
		[[nodiscard]] static std::optional<std::map<uint32_t, float>> GetAxisValues(IDWriteFontFace5* face);

		// Pixels of a run drawn turned, width by height with channels bytes each, turned back: rows become columns, the last on the left. A pixel's
		// channels stay in order, now top to bottom.
		[[nodiscard]] static std::vector<uint8_t> TurnBack(const std::vector<uint8_t>& pixels, int width, int height, int channels);

		// The box of a run drawn turned, turned back.
		[[nodiscard]] static RECT TurnBack(const RECT& turned);

		// turned draws the run turned a quarter, its screen's (x, y) at (y, -x), so ClearType's horizontal stripes are vertical when turned back
		// (TurnBack): antialiased both ways and unhinted, as both are then needed across the text.
		[[nodiscard]] IDWriteGlyphRunAnalysisPtr CreateAnalysis(
			IDWriteFontFace5* face,
			float px,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			float originY,
			const DirectWriteParams& parameters,
			const Matrix2x2& transform,
			DWRITE_TEXT_ANTIALIAS_MODE antialiasMode,
			bool turned) const;
	};
}
