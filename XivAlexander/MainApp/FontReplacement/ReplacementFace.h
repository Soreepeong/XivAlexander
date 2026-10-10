#pragma once

#include "MainApp/FontReplacement/ElementFonts.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FaceElement {
		const FontChanger::Structs::FaceElement& m_def;
		std::unique_ptr<IElementFont> m_font;

	public:
		FaceElement(const FontChanger::Structs::FaceElement& def, std::unique_ptr<IElementFont> font)
			: m_def(def), m_font(std::move(font)) {}

		[[nodiscard]] const FontChanger::Structs::FaceElement& Def() const { return m_def; }

		[[nodiscard]] IElementFont& Font() const { return *m_font; }

		// Non-null if its glyphs are shaped with DirectWrite: a font's, not merged or images.
		[[nodiscard]] OutlineElementFont* Shaped() const { return dynamic_cast<OutlineElementFont*>(m_font.get()); }

		[[nodiscard]] bool DrawsGame() const { return dynamic_cast<GameElementFont*>(m_font.get()) != nullptr; }

		// In its ranges; its font may still lack it.
		[[nodiscard]] bool Contains(char32_t codepoint) const;
	};

	// A preset face made usable (xivres merged_fixed_size_font). Sizes are relative to the first element, which gives the line metrics: an element
	// draws at px * element.Size / first.Size; face pixel values (letter spacing, offsets, Empty metrics) scale by px / first.Size.
	// Codepoints go to elements in order (has = in ranges and in its font): AddNew if none has it yet, AddAll always, Replace only over an earlier one.
	// The rest are the game's, or with SystemFallback the system fonts' (DirectWrite's fallback when shaped, else the built-in face's).
	// Glyph merging elements have only merged codepoints, glyph image elements only those with files; both are drawn glyph by glyph, not shaped.
	class ReplacementFace {
		GlyphRasterizer& m_rasterizer;
		std::shared_ptr<const FontChanger::Structs::Face> m_def;
		std::vector<std::unique_ptr<FaceElement>> m_elements;
		FontChanger::Structs::FaceElement m_gameElementDef;
		std::unique_ptr<FaceElement> m_gameElement;
		ReplacementFace* m_fallback;
		bool m_systemFallback;
		float m_referenceSize;
		FaceElement* m_primary = nullptr;
		DWRITE_MEASURING_MODE m_measureMode = DWRITE_MEASURING_MODE_NATURAL;
		std::map<std::pair<GameFont*, char32_t>, FaceElement*> m_elementCache;
		std::map<std::pair<const FaceElement*, float>, float> m_referenceAdvances;

	public:
		// With systemFallback, characters no element has are drawn with system fonts: fallback's glyph by glyph.
		ReplacementFace(GlyphRasterizer& rasterizer, std::shared_ptr<const FontChanger::Structs::Face> def, bool systemFallback, ReplacementFace* fallback);
		ReplacementFace(const ReplacementFace&) = delete;
		ReplacementFace& operator=(const ReplacementFace&) = delete;

		// Segoe UI, the game's icon font, fonts for scripts Segoe UI lacks, then the system's fallback.
		static std::unique_ptr<ReplacementFace> CreateBuiltIn(GlyphRasterizer& rasterizer);

		// For game fonts no preset covers: the game's glyphs, and with systemFallback, system fonts (fallback's glyph by glyph) for what they lack.
		static std::unique_ptr<ReplacementFace> CreateGame(GlyphRasterizer& rasterizer, bool systemFallback, ReplacementFace* fallback);

		[[nodiscard]] bool SystemFallback() const { return m_systemFallback; }

		// The first element's size, which the face's sizes are relative to.
		[[nodiscard]] float ReferenceSize() const { return m_referenceSize; }

		// The first shaped element (whose font text is laid out in by default), or nullptr.
		[[nodiscard]] FaceElement* Primary() const { return m_primary; }

		// The primary element's, for all the face's glyphs.
		[[nodiscard]] DWRITE_MEASURING_MODE MeasureMode() const { return m_measureMode; }

		[[nodiscard]] float GetElementPx(const FaceElement& element, float px) const;

		// value is at the reference size; rounded.
		[[nodiscard]] int ScalePixels(float value, float px) const;

		// One that DrawsGame if the game's glyph is used; nullptr if the system's fonts draw it.
		[[nodiscard]] FaceElement* GetElement(char32_t codepoint, GameFont* game);

		// Applies the element's replacements.
		[[nodiscard]] static char32_t GetDrawnCodepoint(const FaceElement& element, char32_t codepoint);

		// From the first element, rounded to whole pixels.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, GameFont* game);

		// In its cell (Wrap); nullopt if the game's glyph is to be used.
		[[nodiscard]] std::optional<RasterGlyph> TryRasterize(char32_t codepoint, float px, GameFont* game);

		// As xivres wrapping_fixed_size_font: monospaced cell (squeezed draws narrower), letter spacing on the advance, horizontal offset on the ink
		// (not the pen), moved down by vertical alignment and baseline shift. glyph's advance is the whole pixels it takes, unadjusted.
		[[nodiscard]] RasterGlyph Wrap(const FaceElement& element, RasterGlyph glyph, float px, GameFont* game, const std::function<RasterGlyph(float)>& squeezed);

		[[nodiscard]] int GetLetterSpacing(const FaceElement* element, float px) const;

		// One glyph or a shaped cluster, for a shaped element or the system's fonts (nullptr); as OutlineElementFont::RasterizeRun, measured as the face.
		[[nodiscard]] RasterGlyph RasterizeRun(
			const FaceElement* element,
			IDWriteFontFace* face,
			float size,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			float squeezeX = 1) const;

	private:
		// As FontChanger's FaceElement::GetBaseFont: by renderer, with gamma and glyph merging around it. A font that isn't installed has no glyphs.
		static std::unique_ptr<IElementFont> CreateFont(const FontChanger::Structs::FaceElement& def, GlyphRasterizer& rasterizer, const std::string& faceName);

		// DirectWrite and FreeType fonts, glyph merging's texts' too.
		static std::vector<OutlineElementFont*> OutlineFonts(IElementFont& font);

		// Line box alignment in the face's (xivres merged_fixed_size_font get_vertical_adjustment) plus the element's baseline shift.
		[[nodiscard]] int GetVerticalShift(const FaceElement& element, float px, GameFont* game);

		// Below the line's top; the first element's.
		[[nodiscard]] float GetRomanBaselineY(float px, GameFont* game);
		[[nodiscard]] float GetRomanBaselineY(const FaceElement& element, float px, GameFont* game);

		// Below the line's top: the first element's ideographic face (or em box) middle, or the line's middle.
		[[nodiscard]] float GetIdeographicCenterY(float px, GameFont* game);
		[[nodiscard]] float GetIdeographicCenterY(const FaceElement& element, float px, GameFont* game);
		[[nodiscard]] float GetIdeographicCenterY(const FaceElement& element, float px, int ascent, int lineHeight);

		[[nodiscard]] std::optional<float> GetBaseline(const FaceElement& element, uint32_t tag, float px);
		[[nodiscard]] LineMetrics GetElementMetrics(const FaceElement& element, float px, GameFont* game);

		// glyph's advance is on screen, unadjusted; non-advancing glyphs stay. Ink wider than a limited cell is redrawn by squeezed(scaleX).
		[[nodiscard]] RasterGlyph Monospace(const FaceElement& element, float px, RasterGlyph glyph, const std::function<RasterGlyph(float)>& squeezed);

		// Cached by size.
		[[nodiscard]] float GetReferenceAdvance(const FaceElement& element, float px, char32_t codepoint);
	};
}
