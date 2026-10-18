#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/ImageFaceSource.h"

#include <FontChanger.Presets/GlyphFiles.h>

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::ImageFaceSource::ImageFaceSource(FontChanger::Structs::GlyphImagesStruct settings, float gamma, const Matrix2x2& transform)
	: m_settings(std::move(settings))
	, m_gamma(gamma)
	, m_transform(transform) {
	const auto set = FontChanger::GlyphFiles::LoadGlyphSet(m_settings);
	for (const auto& codepoint : set->Glyphs | std::views::keys)
		m_codepoints.insert(codepoint);
}

std::unique_ptr<FontReplacement::ImageFaceSource> FontReplacement::ImageFaceSource::Create(const FontChanger::Structs::FontElement& element) {
	return std::unique_ptr<ImageFaceSource>(new ImageFaceSource(element.RendererSpecific.GlyphImages, element.Gamma, Matrix2x2::Of(element.Transform)));
}

const std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>& FontReplacement::ImageFaceSource::GetFont(float px, const Matrix2x2& transform) {
	const auto key = std::make_tuple(px, transform.M11, transform.M12, transform.M21, transform.M22);
	auto it = m_fonts.find(key);
	if (it == m_fonts.end())
		it = m_fonts.emplace(key, FontChanger::GlyphFiles::CreateFont(m_settings, px, m_gamma, transform.ToMatrix())).first;
	return it->second;
}

FontReplacement::LineMetrics FontReplacement::ImageFaceSource::GetLineMetrics(float px) {
	const auto& font = GetFont(px, m_transform);
	return {font->ascent(), font->line_height()};
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::ImageFaceSource::RasterizeGlyph(float px, uint32_t glyphId) {
	return RasterizeFixedSizeFontGlyph(*GetFont(px, m_transform), static_cast<char32_t>(glyphId));
}

float FontReplacement::ImageFaceSource::GetKerning(float px, uint32_t leftGlyphId, uint32_t rightGlyphId) {
	const auto& pairs = GetFont(px, m_transform)->all_kerning_pairs();
	const auto it = pairs.find({static_cast<char32_t>(leftGlyphId), static_cast<char32_t>(rightGlyphId)});
	return it == pairs.end() ? 0.f : static_cast<float>(it->second);
}
