#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Outline/LookupFont.h"

struct FT_FaceRec_;

namespace FontChanger::FixedSizeFont {
	class fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	// A font's glyphs, shaped by DirectWrite and drawn by DirectWrite or FreeType (xivres directwrite_ and freetype_fixed_size_font).
	class OutlineFaceSource final : public IFaceSource {
		class TextFont;

		FontServices& m_services;
		FontChanger::Structs::FontElement m_def;
		std::unique_ptr<LookupFont> m_font;

		// nullptr when DirectWrite draws; owned by FreeTypeGlyphRasterizer.
		FT_FaceRec_* m_freeTypeFace;

		std::optional<std::vector<uint8_t>> m_gamma;
		std::string m_fontName;
		Matrix2x2 m_transform;
		DWRITE_MEASURING_MODE m_measureMode = DWRITE_MEASURING_MODE_NATURAL;
		std::optional<std::set<char32_t>> m_codepoints;
		std::unique_ptr<OutlineFaceSource> m_italic;
		bool m_italicMade = false;
		std::optional<std::map<uint32_t, int>> m_baselines;
		std::optional<std::vector<DWRITE_FONT_FEATURE>> m_features;

		OutlineFaceSource(FontServices& services, FontChanger::Structs::FontElement def, std::unique_ptr<LookupFont> font, FT_FaceRec_* freeTypeFace, std::string fontName);

	public:
		OutlineFaceSource(const OutlineFaceSource&) = delete;
		OutlineFaceSource& operator=(const OutlineFaceSource&) = delete;
		~OutlineFaceSource() override;

		// nullptr if the family isn't installed.
		static std::unique_ptr<OutlineFaceSource> Create(FontServices& services, const FontChanger::Structs::FontElement& def, const std::string& fontName);

		void SetMeasureMode(DWRITE_MEASURING_MODE value) override;

		const std::set<char32_t>& Codepoints() override;
		uint32_t GetGlyphId(char32_t codepoint) override;
		float GetAdvance(float px, uint32_t glyphId) override;
		float GetScreenAdvance(float px, const GlyphRun& run) override;
		LineMetrics GetLineMetrics(float px) override { return GetLineMetrics(px, m_transform); }
		std::optional<float> TryGetBaseline(float px, Baseline baseline) override;
		std::pair<IFaceSource*, bool> GetItalic() override;
		int Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) override;
		std::optional<RasterGlyph> Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options = {}) override;

		// transform takes the element's transformation's place; synthesis's still applies.
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> CreateMergeTextFont(float size, const Matrix2x2& transform);

		[[nodiscard]] static uint32_t GetBaselineTag(Baseline baseline);

	private:
		static std::unique_ptr<OutlineFaceSource> FromFont(FontServices& services, FontChanger::Structs::FontElement def, std::unique_ptr<LookupFont> font, const std::string& fontName);

		// Excludes oblique simulations, which aren't the family's own italic.
		[[nodiscard]] static bool IsRealItalic(IDWriteFontFace5* face);

		// Compares the files, since a family's styles often have the same glyph count; simulations and axis values are ignored.
		[[nodiscard]] static bool SameFont(IDWriteFontFace5* a, IDWriteFontFace5* b);

		// Scales vertically as glyphs transformed by transform are.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, const Matrix2x2& transform) const;

		// FreeType's emboldening advances each glyph further than DirectWrite lays it out.
		[[nodiscard]] float GetExtraAdvance(float px) const;

		// originX is on screen, after the transformation; originY places the baseline (glyph merging).
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

		// Side bearings give the ink's extent in fractional pixels, though antialiased pixels may reach past them.
		[[nodiscard]] std::pair<float, float> GetInkExtent(float size, uint16_t glyph, const Matrix2x2& transform);

		// nullptr keeps DirectWrite's defaults; a feature set would turn them off, so they are added unless the lookup sets them.
		const std::vector<DWRITE_FONT_FEATURE>* GetFeatures();
	};
}
