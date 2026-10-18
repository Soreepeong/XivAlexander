#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

struct FT_FaceRec_;

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A font's glyphs drawn with FreeType (xivres freetype_fixed_size_font), with its emboldening, which advances them further than DirectWrite lays
	// them out. The FreeType face is owned by FreeTypeGlyphRasterizer.
	class FreeTypeFaceSource final : public OutlineFaceSource {
		FT_FaceRec_* m_freeTypeFace;

	public:
		FreeTypeFaceSource(FontServices& services, FontChanger::Structs::FontElement def, std::unique_ptr<LookupFont> font, FT_FaceRec_* freeTypeFace, std::string fontName)
			: OutlineFaceSource(services, std::move(def), std::move(font), std::move(fontName))
			, m_freeTypeFace(freeTypeFace) {}

	protected:
		float GetExtraAdvance(float emSize) const override { return Font().Embolden() * emSize; }

		RasterGlyph DrawRun(
			float size,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const Matrix2x2& transform,
			float originY) override;

		RECT MeasureRun(float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const Matrix2x2& transform) override;
	};
}
