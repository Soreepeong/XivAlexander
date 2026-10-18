#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Outline/FreeTypeFaceSource.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::RasterGlyph FontReplacement::FreeTypeFaceSource::DrawRun(
	float size,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	const Matrix2x2& transform,
	float originY) {
	const auto axes = Font().GetAxisValues(size);
	return Services().FreeType().RasterizeRun(
		m_freeTypeFace, size, glyphs, advances, offsets, count, originX, advance, FreeTypeParams::Of(Def().RendererSpecific.FreeType), transform, &axes,
		Font().Embolden(), originY);
}

RECT FontReplacement::FreeTypeFaceSource::MeasureRun(float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const Matrix2x2& transform) {
	constexpr auto advance = 0.f;
	const auto g = DrawRun(size, &glyph, &advance, &offset, 1, 0, 0, transform, 0);
	return {g.Left, g.Top, g.Left + g.Width, g.Top + g.Height};
}
