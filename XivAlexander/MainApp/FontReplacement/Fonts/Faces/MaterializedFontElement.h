#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Records/FontAlignment.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// At a font size px an element draws at px * Size / reference and scales its pixel values by px / reference, the reference being the font's
	// first element's size.
	class MaterializedFontElement {
		FontChanger::Structs::FontElement m_def;
		std::unique_ptr<IFaceSource> m_source;
		float m_referenceSize;
		std::map<float, float> m_referenceAdvances;

	public:
		MaterializedFontElement(FontChanger::Structs::FontElement def, std::unique_ptr<IFaceSource> source, float referenceSize);
		MaterializedFontElement(const MaterializedFontElement&) = delete;
		MaterializedFontElement& operator=(const MaterializedFontElement&) = delete;
		~MaterializedFontElement();

		[[nodiscard]] const FontChanger::Structs::FontElement& Def() const { return m_def; }

		// nullptr when the glyphs are copied from the replaced game font.
		[[nodiscard]] IFaceSource* Source() const { return m_source.get(); }

		[[nodiscard]] bool DrawsGame() const { return !m_source; }

		// Its source may still lack the glyph.
		[[nodiscard]] bool Contains(char32_t codepoint) const;

		[[nodiscard]] bool Has(char32_t codepoint) const;

		[[nodiscard]] char32_t GetDrawnCodepoint(char32_t codepoint) const;

		[[nodiscard]] float GetPx(float fontPx) const { return fontPx * m_def.Size / m_referenceSize; }

		[[nodiscard]] int GetLetterSpacing(float fontPx) const { return ScalePixels(m_def.WrapModifiers.LetterSpacing, fontPx); }

		[[nodiscard]] LineMetrics GetLineMetrics(float fontPx) const;

		// Measured down from the line's top.
		[[nodiscard]] float GetRomanBaselineY(float fontPx, int ascent) const;

		// Measured down from the line's top.
		[[nodiscard]] float GetIdeographicCenterY(float fontPx, int ascent, int lineHeight) const;

		// As xivres wrapping_fixed_size_font; glyph is run as source (the element's, or its italic) drew it at originX, so that monospacing can
		// draw it again narrower.
		[[nodiscard]] RasterGlyph Wrap(RasterGlyph glyph, float fontPx, const FontAlignment& font, IFaceSource& source, const GlyphRun& run, float originX = 0);

	private:
		// value is in pixels at the font's reference size.
		[[nodiscard]] int ScalePixels(float value, float fontPx) const { return static_cast<int>(std::round(value * fontPx / m_referenceSize)); }

		// As xivres merged_fixed_size_font get_vertical_adjustment, plus the vertical offset.
		[[nodiscard]] int GetVerticalShift(float fontPx, const FontAlignment& font) const;

		[[nodiscard]] RasterGlyph Monospace(float fontPx, RasterGlyph glyph, IFaceSource& source, const GlyphRun& run, float originX);

		[[nodiscard]] float GetReferenceAdvance(float fontPx, char32_t codepoint);
	};
}
