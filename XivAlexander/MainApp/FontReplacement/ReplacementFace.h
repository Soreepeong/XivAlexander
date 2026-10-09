#pragma once

#include "MainApp/FontReplacement/ElementFonts.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// An element of a face: its definition, and the font it draws with (IElementFont).
	class FaceElement {
		const FontChanger::Structs::FaceElement& m_def;
		std::unique_ptr<IElementFont> m_font;

	public:
		FaceElement(const FontChanger::Structs::FaceElement& def, std::unique_ptr<IElementFont> font)
			: m_def(def), m_font(std::move(font)) {}

		[[nodiscard]] const FontChanger::Structs::FaceElement& Def() const { return m_def; }

		[[nodiscard]] IElementFont& Font() const { return *m_font; }

		// Gets the element's font if its glyphs are shaped with DirectWrite: those of a font, not merged or images.
		[[nodiscard]] OutlineElementFont* Shaped() const { return dynamic_cast<OutlineElementFont*>(m_font.get()); }

		// Gets whether the game draws the element's glyphs (its own).
		[[nodiscard]] bool DrawsGame() const { return dynamic_cast<GameElementFont*>(m_font.get()) != nullptr; }

		// Gets whether the element may draw a codepoint (it is in its ranges).
		[[nodiscard]] bool Contains(char32_t codepoint) const;
	};

	// A face of a preset made usable (xivres merged_fixed_size_font): its elements' fonts, which element draws each
	// codepoint, how its glyphs are adjusted, and its line metrics at a drawn size.
	//
	// Sizes in a face are relative to its first element: at a drawn size px, an element draws at px * element.Size /
	// first.Size, and pixel values in the face (letter spacing, offsets, Empty metrics) scale by px / first.Size. The first
	// element gives the line metrics.
	//
	// A codepoint goes to the elements in order: an element has it if it is in its ranges and its font has it; AddNew takes it
	// if no element has yet, AddAll always, Replace only from an earlier element. Characters no element has are the game's,
	// or with SystemFallback, the system fonts': in shaped text DirectWrite's fallback, glyph by glyph the built-in face's.
	//
	// An element with glyph merging has only its merged codepoints, and an element of glyph images only those it has files
	// for; both are drawn glyph by glyph, not shaped.
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
		// Sets a face up. With systemFallback, characters no element has are drawn with system fonts: fallback's glyph by
		// glyph.
		ReplacementFace(GlyphRasterizer& rasterizer, std::shared_ptr<const FontChanger::Structs::Face> def, bool systemFallback, ReplacementFace* fallback);
		ReplacementFace(const ReplacementFace&) = delete;
		ReplacementFace& operator=(const ReplacementFace&) = delete;

		// Makes the built-in face: Segoe UI, the game's icon font, fonts for scripts Segoe UI lacks, then the system's
		// fallback.
		static std::unique_ptr<ReplacementFace> CreateBuiltIn(GlyphRasterizer& rasterizer);

		// Makes the face of game fonts no preset gives one: the game's glyphs, and for characters the game font lacks, with
		// systemFallback, system fonts (fallback's glyph by glyph).
		static std::unique_ptr<ReplacementFace> CreateGame(GlyphRasterizer& rasterizer, bool systemFallback, ReplacementFace* fallback);

		// Gets whether characters no element has are drawn with system fonts instead of the game's glyphs.
		[[nodiscard]] bool SystemFallback() const { return m_systemFallback; }

		// Gets the size the face's sizes are relative to: its first element's.
		[[nodiscard]] float ReferenceSize() const { return m_referenceSize; }

		// Gets the first element whose glyphs are shaped (the font text is laid out in by default), or nullptr.
		[[nodiscard]] FaceElement* Primary() const { return m_primary; }

		// Gets how the face's text is measured: its primary element's, for all its glyphs.
		[[nodiscard]] DWRITE_MEASURING_MODE MeasureMode() const { return m_measureMode; }

		// Gets the size an element draws at for a drawn size.
		[[nodiscard]] float GetElementPx(const FaceElement& element, float px) const;

		// Gets a pixel value of the face (at its reference size) at a drawn size, rounded.
		[[nodiscard]] int ScalePixels(float value, float px) const;

		// Gets the element that draws a codepoint (one that DrawsGame if the game's glyph is used), or nullptr if the system's
		// fonts draw it.
		[[nodiscard]] FaceElement* GetElement(char32_t codepoint, GameFont* game);

		// Gets the codepoint whose glyph an element draws for a codepoint (its replacements).
		[[nodiscard]] static char32_t GetDrawnCodepoint(const FaceElement& element, char32_t codepoint);

		// Gets the line metrics at a drawn size: from the first element, rounded to whole pixels.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, GameFont* game);

		// Rasterizes a codepoint at a drawn size, in its cell (Wrap); nullopt if the game's glyph is to be used.
		[[nodiscard]] std::optional<RasterGlyph> TryRasterize(char32_t codepoint, float px, GameFont* game);

		// Adjusts a glyph of an element at a drawn size as the face says (xivres wrapping_fixed_size_font): placed in its
		// monospaced cell (Monospace; squeezed draws it narrower), its letter spacing added to the advance, its horizontal
		// offset to the ink (not the pen), and moved down by the element's vertical alignment in the face and baseline shift.
		// glyph's advance is the whole pixels it takes, unadjusted.
		[[nodiscard]] RasterGlyph Wrap(const FaceElement& element, RasterGlyph glyph, float px, GameFont* game, const std::function<RasterGlyph(float)>& squeezed);

		// Gets the letter spacing Wrap adds to an element's advances at a drawn size (0 for none).
		[[nodiscard]] int GetLetterSpacing(const FaceElement* element, float px) const;

		// Rasterizes a run of glyphs of a font face (one glyph, or a shaped cluster) for an element whose glyphs are shaped,
		// or for the system's fonts (nullptr): as OutlineElementFont::RasterizeRun, measured as the face measures.
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
		// Makes an element's font (as FontChanger's FaceElement::GetBaseFont makes them): by its renderer, with its gamma, and
		// glyph merging around it. A font that isn't installed has no glyphs.
		static std::unique_ptr<IElementFont> CreateFont(const FontChanger::Structs::FaceElement& def, GlyphRasterizer& rasterizer, const std::string& faceName);

		// Gets the fonts of DirectWrite and FreeType in an element's font (glyph merging's texts' too).
		static std::vector<OutlineElementFont*> OutlineFonts(IElementFont& font);

		// Gets how far an element's glyphs are moved down from the face's baseline at a drawn size: by the face's vertical
		// alignment of the element's line box in the face's (xivres merged_fixed_size_font get_vertical_adjustment), and the
		// element's baseline shift.
		[[nodiscard]] int GetVerticalShift(const FaceElement& element, float px, GameFont* game);

		// Gets how far below the line's top the roman baseline of the face is: its first element's.
		[[nodiscard]] float GetRomanBaselineY(float px, GameFont* game);
		[[nodiscard]] float GetRomanBaselineY(const FaceElement& element, float px, GameFont* game);

		// Gets how far below the line's top the middle of the ideographic face (or em box) of the face is: its first
		// element's, or the middle of the line.
		[[nodiscard]] float GetIdeographicCenterY(float px, GameFont* game);
		[[nodiscard]] float GetIdeographicCenterY(const FaceElement& element, float px, GameFont* game);
		[[nodiscard]] float GetIdeographicCenterY(const FaceElement& element, float px, int ascent, int lineHeight);

		[[nodiscard]] std::optional<float> GetBaseline(const FaceElement& element, uint32_t tag, float px);
		[[nodiscard]] LineMetrics GetElementMetrics(const FaceElement& element, float px, GameFont* game);

		// Places a glyph of an element (its advance on screen, unadjusted) in a cell as the element's monospacing says: the
		// advance clamped into its limits, the ink aligned in the cell, and ink wider than a limited cell drawn narrower with
		// squeezed (given the horizontal scale). Glyphs that don't advance stay as they are.
		[[nodiscard]] RasterGlyph Monospace(const FaceElement& element, float px, RasterGlyph glyph, const std::function<RasterGlyph(float)>& squeezed);

		// Gets the advance of an element's monospacing reference character at a drawn size (kept by size).
		[[nodiscard]] float GetReferenceAdvance(const FaceElement& element, float px, char32_t codepoint);
	};
}
