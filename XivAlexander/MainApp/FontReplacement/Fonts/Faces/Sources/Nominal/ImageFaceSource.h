#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"
#include "MainApp/FontReplacement/Utilities/Matrix2x2.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Port of xivres image_fixed_size_font, drawn by FontChanger GlyphFiles; glyph IDs are codepoints.
	class ImageFaceSource final : public NominalFaceSource {
		FontChanger::Structs::GlyphImagesStruct m_settings;
		float m_gamma;
		Matrix2x2 m_transform;
		std::set<char32_t> m_codepoints;
		std::map<std::tuple<float, float, float, float, float>, std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>> m_fonts;

		ImageFaceSource(FontChanger::Structs::GlyphImagesStruct settings, float gamma, const Matrix2x2& transform);

	public:
		static std::unique_ptr<ImageFaceSource> Create(const FontChanger::Structs::FontElement& element);

		const std::set<char32_t>& Codepoints() override { return m_codepoints; }
		uint32_t GetGlyphId(char32_t codepoint) override { return m_codepoints.contains(codepoint) ? static_cast<uint32_t>(codepoint) : 0u; }
		LineMetrics GetLineMetrics(float px) override;

		[[nodiscard]] const std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>& GetFont(float px, const Matrix2x2& transform);

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override;
		float GetKerning(float px, uint32_t leftGlyphId, uint32_t rightGlyphId) override;
	};
}
