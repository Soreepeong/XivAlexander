#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"

namespace FontChanger::FixedSizeFont {
	class glyph_merging_fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;
	class OutlineFaceSource;

	// Port of xivres glyph_merging_fixed_size_font, drawn by FontChanger.FixedSizeFont; glyph IDs are codepoints.
	class MergingFaceSource final : public NominalFaceSource {
		class BaseFont;

		std::unique_ptr<IFaceSource> m_inner;
		FontChanger::Structs::FontElement m_def;
		std::unique_ptr<OutlineFaceSource> m_ownTextOutline;
		OutlineFaceSource* m_textOutline;
		std::set<char32_t> m_codepoints;
		std::map<float, std::shared_ptr<FontChanger::FixedSizeFont::glyph_merging_fixed_size_font>> m_fonts;

		MergingFaceSource(std::unique_ptr<IFaceSource> inner, FontChanger::Structs::FontElement def, std::unique_ptr<OutlineFaceSource> ownTextOutline);

	public:
		~MergingFaceSource() override;

		static std::unique_ptr<MergingFaceSource> Create(std::unique_ptr<IFaceSource> inner, const FontChanger::Structs::FontElement& def, FontServices& services, const std::string& fontName);

		const std::set<char32_t>& Codepoints() override { return m_codepoints; }
		uint32_t GetGlyphId(char32_t codepoint) override { return m_codepoints.contains(codepoint) ? static_cast<uint32_t>(codepoint) : 0u; }
		LineMetrics GetLineMetrics(float px) override { return m_inner->GetLineMetrics(px); }
		std::optional<float> TryGetBaseline(float px, Baseline baseline) override { return m_inner->TryGetBaseline(px, baseline); }
		void SetMeasureMode(DWRITE_MEASURING_MODE value) override;

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override;

	private:
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> CreateTextFont(float size, float condense);
		[[nodiscard]] std::optional<RasterGlyph> DrawInner(char32_t codepoint, float px);
	};
}
