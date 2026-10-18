#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Port of xivres empty_fixed_size_font; ascent and lineHeight are in pixels at size.
	class EmptyFaceSource final : public NominalFaceSource {
		float m_size;
		float m_ascent;
		float m_lineHeight;

	public:
		EmptyFaceSource(float size, float ascent, float lineHeight)
			: m_size(size)
			, m_ascent(ascent)
			, m_lineHeight(lineHeight) {}

		const std::set<char32_t>& Codepoints() override {
			static const std::set<char32_t> s_none;
			return s_none;
		}

		uint32_t GetGlyphId(char32_t codepoint) override { return 0; }

		float GetAdvance(float px, uint32_t glyphId) override { return 0; }

		LineMetrics GetLineMetrics(float px) override {
			if (m_size <= 0)
				return {};
			return {static_cast<int>(std::round(m_ascent * px / m_size)), static_cast<int>(std::round(m_lineHeight * px / m_size))};
		}

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override { return std::nullopt; }
	};
}
