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

		// Game thread, before glyphs are made again.
		void SetSubpixelOrder(SubpixelOrder order) { m_subpixelOrder = order; }

		// DirectWrite has no ClearType for vertical stripes, so without FreeType it draws them turned a quarter; set on the game thread, before glyphs are
		// made again.
		[[nodiscard]] bool VerticalWithFreeType() const { return m_verticalWithFreeType; }
		void SetVerticalWithFreeType(bool value) { m_verticalWithFreeType = value; }

		// originX is the pen's fraction of a pixel so that a cluster lands where shaping put it, and originY places the baseline for glyph merging.
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

	private:
		[[nodiscard]] bool UsesSubpixel(const DirectWriteParams& parameters) const;

		// nullopt leaves the run to DirectWrite when FreeType can't open the face or it has only bitmaps, which have no subpixel coverage.
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

		// turned draws the run turned a quarter, so that ClearType's horizontal stripes are vertical once turned back.
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
			bool subpixel,
			bool turned) const;
	};
}
