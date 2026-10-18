#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A font's glyphs drawn with DirectWrite (xivres directwrite_fixed_size_font), measured as the font measures.
	class DirectWriteFaceSource final : public OutlineFaceSource {
	public:
		DirectWriteFaceSource(FontServices& services, FontChanger::Structs::FontElement def, std::unique_ptr<LookupFont> font, std::string fontName)
			: OutlineFaceSource(services, std::move(def), std::move(font), std::move(fontName)) {}

	protected:
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

	private:
		[[nodiscard]] DirectWriteParams GetParams() const;
	};
}
