#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"
#include "MainApp/FontReplacement/Utilities/Matrix2x2.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Glyph images (FontChanger GlyphFiles, xivres image_fixed_size_font): SVG documents and bitmaps by codepoint, one glyph per codepoint, with
	// font.json's units, line metrics and pair kerning. Glyph IDs are codepoints; drawn by one font per size and transformation.
	class ImageFaceSource final : public NominalFaceSource {
		FontChanger::Structs::GlyphImagesStruct m_settings;
		float m_gamma;
		Matrix2x2 m_transform;
		std::set<char32_t> m_codepoints;
		std::map<std::tuple<float, float, float, float, float>, std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>> m_fonts;

		ImageFaceSource(FontChanger::Structs::GlyphImagesStruct settings, float gamma, const Matrix2x2& transform);

	public:
		// Files that can't be read are left out.
		static std::unique_ptr<ImageFaceSource> Create(const FontChanger::Structs::FontElement& element);

		const std::set<char32_t>& Codepoints() override { return m_codepoints; }
		uint32_t GetGlyphId(char32_t codepoint) override { return m_codepoints.contains(codepoint) ? static_cast<uint32_t>(codepoint) : 0u; }
		LineMetrics GetLineMetrics(float px) override;

		// The font glyph merging's texts are drawn with at a size, transformed.
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> CreateMergeTextFont(float size, const Matrix2x2& transform);

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override;
		float GetKerning(float px, uint32_t leftGlyphId, uint32_t rightGlyphId) override;

	private:
		// With the gamma.
		[[nodiscard]] const std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>& GetFont(float px, const Matrix2x2& transform);
	};
}
