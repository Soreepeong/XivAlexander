#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Outline/LookupFont.h"

namespace FontChanger::FixedSizeFont {
	class fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	// A font's glyphs: found by the element's lookup, shaped with DirectWrite, and drawn by DirectWriteFaceSource or FreeTypeFaceSource. Glyph IDs
	// are the font's glyph indices.
	class OutlineFaceSource : public IFaceSource {
		class TextFont;

		FontServices& m_services;
		FontChanger::Structs::FontElement m_def;
		std::unique_ptr<LookupFont> m_font;
		std::optional<std::vector<uint8_t>> m_gamma;
		std::string m_fontName;
		Matrix2x2 m_transform;
		DWRITE_MEASURING_MODE m_measureMode = DWRITE_MEASURING_MODE_NATURAL;
		std::optional<std::set<char32_t>> m_codepoints;
		std::unique_ptr<OutlineFaceSource> m_italic;
		bool m_italicMade = false;
		std::optional<std::map<uint32_t, int>> m_baselines;
		std::optional<std::vector<DWRITE_FONT_FEATURE>> m_features;

	protected:
		OutlineFaceSource(FontServices& services, FontChanger::Structs::FontElement def, std::unique_ptr<LookupFont> font, std::string fontName);

		[[nodiscard]] FontServices& Services() const { return m_services; }
		[[nodiscard]] const FontChanger::Structs::FontElement& Def() const { return m_def; }
		[[nodiscard]] LookupFont& Font() const { return *m_font; }

	public:
		OutlineFaceSource(const OutlineFaceSource&) = delete;
		OutlineFaceSource& operator=(const OutlineFaceSource&) = delete;
		~OutlineFaceSource() override;

		// Made as the element draws it (its renderer, for synthesis); nullptr if the family isn't installed.
		static std::unique_ptr<OutlineFaceSource> Create(FontServices& services, const FontChanger::Structs::FontElement& def, const std::string& fontName);

		// On screen: synthesis first, then the element's.
		[[nodiscard]] const Matrix2x2& Transform() const { return m_transform; }

		// The font's, which its text is shaped with.
		[[nodiscard]] DWRITE_MEASURING_MODE MeasureMode() const { return m_measureMode; }
		void SetMeasureMode(DWRITE_MEASURING_MODE value);

		const std::set<char32_t>& Codepoints() override;
		uint32_t GetGlyphId(char32_t codepoint) override;
		float GetAdvance(float px, uint32_t glyphId) override;
		float GetScreenAdvance(float px, const GlyphRun& run) override;
		LineMetrics GetLineMetrics(float px) override { return GetLineMetrics(px, m_transform); }
		std::optional<float> TryGetBaseline(float px, Baseline baseline) override;

		// Its family's italic, drawn as this one is; itself if its font is an italic, or (not real) if the family has none of its own.
		std::pair<IFaceSource*, bool> GetItalic() override;

		// With DirectWrite's shaping of the font: its features and language, as the font measures.
		int Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) override;

		std::optional<RasterGlyph> Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options = {}) override;

		// The font glyph merging's texts are drawn with at a size, transformed on top of synthesis's.
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> CreateMergeTextFont(float size, const Matrix2x2& transform);

		[[nodiscard]] static uint32_t GetBaselineTag(Baseline baseline);

	protected:
		// How much further glyphs advance than DirectWrite lays them out: none, but for FreeType's emboldening.
		[[nodiscard]] virtual float GetExtraAdvance(float emSize) const { return 0; }

		// As RasterizeRun, without the gamma.
		[[nodiscard]] virtual RasterGlyph DrawRun(
			float size,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const Matrix2x2& transform,
			float originY) = 0;

		// The pixels a glyph covers, moved by its offset, with the pen at the origin.
		[[nodiscard]] virtual RECT MeasureRun(float size, uint16_t glyph, DWRITE_GLYPH_OFFSET offset, const Matrix2x2& transform) = 0;

	private:
		// FreeType where the element says so and for fonts with only bitmaps (which DirectWrite doesn't draw), else DirectWrite.
		static std::unique_ptr<OutlineFaceSource> FromFont(FontServices& services, FontChanger::Structs::FontElement def, std::unique_ptr<LookupFont> font, const std::string& fontName);

		// Scaled vertically as glyphs transformed by transform are.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, const Matrix2x2& transform) const;

		// One glyph, a shaped cluster, or a text of glyph merging, transformed, with the gamma; originX is on screen, after the transformation,
		// and originY places the baseline (glyph merging).
		[[nodiscard]] RasterGlyph RasterizeRun(
			float size,
			const uint16_t* glyphs,
			const float* advances,
			const DWRITE_GLYPH_OFFSET* offsets,
			uint32_t count,
			float originX,
			int advance,
			const Matrix2x2& transform,
			float originY = 0);

		// Horizontal, in pixels from the origin, from the outline's side bearings.
		[[nodiscard]] std::pair<float, float> GetInkExtent(float size, uint16_t glyph, const Matrix2x2& transform);

		// nullptr for DirectWrite's defaults; else the font's, with the defaults a set of features would otherwise turn off on unless it says otherwise.
		const std::vector<DWRITE_FONT_FEATURE>* GetFeatures();

		// A real italic (or oblique) style of its family, not one synthesized.
		[[nodiscard]] static bool IsRealItalic(IDWriteFontFace5* face);

		// Same file and index, whatever the simulations and axes; files are compared as a family's styles often share glyph counts.
		[[nodiscard]] static bool SameFont(IDWriteFontFace5* a, IDWriteFontFace5* b);
	};
}
