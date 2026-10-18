#pragma once

#include "MainApp/FontReplacement/Fonts/Enums/Baseline.h"
#include "MainApp/FontReplacement/Fonts/Records/GlyphRun.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterizeOptions.h"
#include "MainApp/FontReplacement/Fonts/Records/ScriptToken.h"
#include "MainApp/FontReplacement/Fonts/Records/ShapedCluster.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// In whole pixels.
	struct LineMetrics {
		int Ascent = 0;
		int LineHeight = 0;
	};

	// The glyphs a font element draws (xivres fixed_size_font), at any size: as the element's source, gamma and transformation say, but not yet
	// adjusted by the font (MaterializedFontElement::Wrap). Glyph IDs are the source's own: glyph indices for outline fonts, codepoints for the
	// others; 0 is no glyph. Glyphs are relative to the pen on the baseline; sizes are the element's (the font's drawn size scaled by the element's).
	class IFaceSource {
	public:
		virtual ~IFaceSource() = default;

		[[nodiscard]] virtual const std::set<char32_t>& Codepoints() = 0;

		// Without shaping; 0 if there is none.
		[[nodiscard]] virtual uint32_t GetGlyphId(char32_t codepoint) = 0;

		// As a layout places it, before the source's transformation and emboldening; 0 if none.
		[[nodiscard]] virtual float GetAdvance(float px, uint32_t glyphId) = 0;

		// On screen: the run's advances transformed, with emboldening.
		[[nodiscard]] virtual float GetScreenAdvance(float px, const GlyphRun& run) {
			auto sum = 0.f;
			for (const auto a : run.Advances)
				sum += a;
			return sum;
		}

		[[nodiscard]] virtual LineMetrics GetLineMetrics(float px) = 0;

		// Of the BASE table, in pixels above the glyphs' origin; nullopt if it has none.
		[[nodiscard]] virtual std::optional<float> TryGetBaseline(float px, Baseline baseline) = 0;

		// The source that draws its text in italics, and whether it is a real italic.
		[[nodiscard]] virtual std::pair<IFaceSource*, bool> GetItalic() { return {this, false}; }

		// Into clusters of its glyphs, in logical order; stops before the first cluster it lacks a glyph of. Returns the code units shaped, always
		// at a cluster boundary.
		virtual int Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) = 0;

		// One glyph, or a shaped cluster, into one image; nullopt if the source has none of its glyphs.
		[[nodiscard]] virtual std::optional<RasterGlyph> Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options = {}) = 0;
	};
}
