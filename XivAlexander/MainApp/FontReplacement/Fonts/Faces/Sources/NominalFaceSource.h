#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"

namespace FontChanger::FixedSizeFont {
	class fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Glyph IDs are codepoints, one glyph per codepoint: shaped as each glyph at its advance (and the kerning with the next), drawn one by one at
	// whole pixels.
	class NominalFaceSource : public IFaceSource {
		// Advances are the drawn glyphs' unless a source knows them otherwise; glyphs drawn to measure them are kept until drawn for good.
		std::map<std::pair<float, uint32_t>, float> m_advances;
		std::map<std::pair<float, uint32_t>, std::optional<RasterGlyph>> m_measured;

	public:
		float GetAdvance(float px, uint32_t glyphId) override;

		std::optional<float> TryGetBaseline(float px, Baseline baseline) override { return std::nullopt; }

		// Each codepoint a cluster, the kerning with the next added to its advance; stops before a codepoint it has no glyph of.
		int Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) override;

		// Each glyph at its pen (rounded), merged, squeezed and given its advance as the options say.
		std::optional<RasterGlyph> Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options = {}) override;

	protected:
		// Relative to the pen on the baseline; nullopt if there is none.
		[[nodiscard]] virtual std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) = 0;

		// In pixels.
		[[nodiscard]] virtual float GetKerning(float px, uint32_t leftGlyphId, uint32_t rightGlyphId) { return 0; }

		// Relative to the pen on the baseline; nullopt if it has none.
		[[nodiscard]] static std::optional<RasterGlyph> RasterizeFixedSizeFontGlyph(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint);

	private:
		// Or takes it as it was drawn to measure its advance.
		std::optional<RasterGlyph> DrawGlyph(float px, uint32_t glyphId);
	};
}
