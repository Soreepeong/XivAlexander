#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Outline/DirectWriteFaceSource.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::RasterGlyph FontReplacement::DirectWriteFaceSource::DrawRun(
	float size,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	const Matrix2x2& transform,
	float originY) {
	return Services().DirectWrite().RasterizeRun(Font().GetFace(size), size, glyphs, advances, offsets, count, originX, advance, GetParams(), transform, originY);
}

RECT FontReplacement::DirectWriteFaceSource::MeasureRun(float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const Matrix2x2& transform) {
	constexpr auto advance = 0.f;
	return Services().DirectWrite().GetRunBounds(Font().GetFace(size), size, &glyph, &advance, &offset, 1, GetParams(), transform);
}

FontReplacement::DirectWriteParams FontReplacement::DirectWriteFaceSource::GetParams() const {
	auto parameters = DirectWriteParams::Of(Def().RendererSpecific.DirectWrite);
	parameters.MeasureMode = MeasureMode();
	return parameters;
}
