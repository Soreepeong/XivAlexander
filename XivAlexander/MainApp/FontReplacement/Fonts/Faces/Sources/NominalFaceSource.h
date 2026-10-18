#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/IFaceSource.h"

namespace FontChanger::FixedSizeFont {
	class fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Glyph IDs are codepoints, one glyph each, placed at whole pixels.
	class NominalFaceSource : public IFaceSource {
		// Measuring an advance draws the glyph, which is kept so that drawing it for good needn't repeat that.
		std::map<std::pair<float, uint32_t>, float> m_advances;
		std::map<std::pair<float, uint32_t>, std::optional<RasterGlyph>> m_measured;

	public:
		float GetAdvance(float px, uint32_t glyphId) override;

		std::optional<float> TryGetBaseline(float px, Baseline baseline) override { return std::nullopt; }

		int Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) override;

		std::optional<RasterGlyph> Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options = {}) override;

	protected:
		[[nodiscard]] virtual std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) = 0;

		// In pixels.
		[[nodiscard]] virtual float GetKerning(float px, uint32_t leftGlyphId, uint32_t rightGlyphId) { return 0; }

		[[nodiscard]] static std::optional<RasterGlyph> RasterizeFixedSizeFontGlyph(const FontChanger::FixedSizeFont::fixed_size_font& font, char32_t codepoint);

	private:
		std::optional<RasterGlyph> DrawGlyph(float px, uint32_t glyphId);
	};
}
