#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Records/FontAlignment.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// An element of a font: its definition and source, at sizes relative to the font's reference size (its first element's): at a drawn size px, it
	// draws at px * Size / reference, and its pixel values (letter spacing, offsets) scale by px / reference.
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

		// nullptr for the game font being replaced (copied from the game's textures).
		[[nodiscard]] IFaceSource* Source() const { return m_source.get(); }

		// Its glyphs are the game font's (it has no source of its own).
		[[nodiscard]] bool DrawsGame() const { return !m_source; }

		// In its ranges; its source may still lack it.
		[[nodiscard]] bool Contains(char32_t codepoint) const;

		// In its ranges, and its source has the glyph it draws it with.
		[[nodiscard]] bool Has(char32_t codepoint) const;

		// Its codepoint replacements applied.
		[[nodiscard]] char32_t GetDrawnCodepoint(char32_t codepoint) const;

		// Scaled by its size relative to the font's reference.
		[[nodiscard]] float GetPx(float fontPx) const { return fontPx * m_def.Size / m_referenceSize; }

		[[nodiscard]] int GetLetterSpacing(float fontPx) const { return ScalePixels(m_def.WrapModifiers.LetterSpacing, fontPx); }

		// None without a source.
		[[nodiscard]] LineMetrics GetLineMetrics(float fontPx) const;

		// Below the line's top, with its line's ascent.
		[[nodiscard]] float GetRomanBaselineY(float fontPx, int ascent) const;

		// Below the line's top: the middle of its ideographic face (or em box), with its line's metrics; the middle of the line if it has neither.
		[[nodiscard]] float GetIdeographicCenterY(float fontPx, int ascent, int lineHeight) const;

		// As xivres wrapping_fixed_size_font: monospaced, letter spacing on the advance, horizontal offset on the ink, moved down by its alignment
		// in the font and its vertical offset. glyph is run as source (its source, or its italic) drew it with the pen at originX, drawn again
		// narrower if the cell needs it; its advance is the whole pixels it takes, unadjusted.
		[[nodiscard]] RasterGlyph Wrap(RasterGlyph glyph, float fontPx, const FontAlignment& font, IFaceSource& source, const GlyphRun& run, float originX = 0);

	private:
		// value is at the font's reference size; rounded.
		[[nodiscard]] int ScalePixels(float value, float fontPx) const { return static_cast<int>(std::round(value * fontPx / m_referenceSize)); }

		// The element's line box placed in the font's (xivres merged_fixed_size_font get_vertical_adjustment), plus its vertical offset.
		[[nodiscard]] int GetVerticalShift(float fontPx, const FontAlignment& font) const;

		// The advance clamped into the monospacing's limits, the ink aligned in the cell, and ink wider than a limited cell drawn again narrower.
		// Glyphs that don't advance stay as they are.
		[[nodiscard]] RasterGlyph Monospace(float fontPx, RasterGlyph glyph, IFaceSource& source, const GlyphRun& run, float originX);

		// Of its monospacing reference character, on screen; kept by size.
		[[nodiscard]] float GetReferenceAdvance(float fontPx, char32_t codepoint);
	};
}
