#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"

namespace FontChanger::FixedSizeFont {
	class glyph_merging_fixed_size_font;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;
	class OutlineFaceSource;

	// Merged glyphs (xivres glyph_merging_fixed_size_font) around an element's own source: texts in shapes, for the codepoints the merging maps;
	// the line metrics and baselines are the inner source's. Glyph IDs are codepoints. One merging font per size, drawn by FontChanger.FixedSizeFont.
	class MergingFaceSource final : public NominalFaceSource {
		class BaseFont;

		std::unique_ptr<IFaceSource> m_inner;
		FontChanger::Structs::FontElement m_def;
		FontServices& m_services;

		// The font texts are drawn with (owned unless it is the inner source); nullptr to draw them with the inner source's glyph images.
		std::unique_ptr<OutlineFaceSource> m_ownTextOutline;
		OutlineFaceSource* m_textOutline;

		std::set<char32_t> m_codepoints;
		std::map<float, std::shared_ptr<FontChanger::FixedSizeFont::glyph_merging_fixed_size_font>> m_fonts;

		MergingFaceSource(std::unique_ptr<IFaceSource> inner, FontChanger::Structs::FontElement def, FontServices& services, std::unique_ptr<OutlineFaceSource> ownTextOutline);

	public:
		~MergingFaceSource() override;

		// Texts are drawn with the element's font; glyph images draw them with the font the lookup names, if any, else with their own files.
		static std::unique_ptr<MergingFaceSource> Create(std::unique_ptr<IFaceSource> inner, const FontChanger::Structs::FontElement& def, FontServices& services, const std::string& fontName);

		// Its own and its texts', which the font measures alike.
		[[nodiscard]] std::vector<OutlineFaceSource*> OutlineSources() const;

		const std::set<char32_t>& Codepoints() override { return m_codepoints; }
		uint32_t GetGlyphId(char32_t codepoint) override { return m_codepoints.contains(codepoint) ? static_cast<uint32_t>(codepoint) : 0u; }
		LineMetrics GetLineMetrics(float px) override { return m_inner->GetLineMetrics(px); }
		std::optional<float> TryGetBaseline(float px, Baseline baseline) override { return m_inner->TryGetBaseline(px, baseline); }

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override;

	private:
		// Condensed horizontally: transformed by the element, then the texts' transformation, then the condensing; nullptr if the font isn't there.
		[[nodiscard]] std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> CreateTextFont(float size, float condense);

		// The inner source's own glyph of a codepoint.
		[[nodiscard]] std::optional<RasterGlyph> DrawInner(char32_t codepoint, float px);
	};
}
