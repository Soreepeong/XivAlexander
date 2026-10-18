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

	// As xivres fixed_size_font, before MaterializedFontElement::Wrap places the glyphs: sizes are the element's, and glyphs are relative to the
	// pen on the baseline.
	class IFaceSource {
	public:
		virtual ~IFaceSource() = default;

		[[nodiscard]] virtual const std::set<char32_t>& Codepoints() = 0;

		// Shared by the whole font, so that its elements' text lines up; ignored without DirectWrite.
		virtual void SetMeasureMode(DWRITE_MEASURING_MODE mode) {}

		// Glyph IDs are glyph indices for outline fonts and codepoints otherwise; 0 is no glyph.
		[[nodiscard]] virtual uint32_t GetGlyphId(char32_t codepoint) = 0;

		// Before the source's transformation and emboldening, as layout places it.
		[[nodiscard]] virtual float GetAdvance(float px, uint32_t glyphId) = 0;

		// With the source's transformation and emboldening.
		[[nodiscard]] virtual float GetScreenAdvance(float px, const GlyphRun& run) {
			auto sum = 0.f;
			for (const auto a : run.Advances)
				sum += a;
			return sum;
		}

		[[nodiscard]] virtual LineMetrics GetLineMetrics(float px) = 0;

		// From the BASE table, in pixels above the glyphs' origin.
		[[nodiscard]] virtual std::optional<float> TryGetBaseline(float px, Baseline baseline) = 0;

		// The bool is false if the source has no italic and returns itself.
		[[nodiscard]] virtual std::pair<IFaceSource*, bool> GetItalic() { return {this, false}; }

		// Clusters are in logical order and stop before the first one lacking a glyph; returns the code units shaped, at a cluster boundary.
		virtual int Shape(float px, std::wstring_view text, const ScriptToken& script, std::vector<ShapedCluster>& clusters) = 0;

		[[nodiscard]] virtual std::optional<RasterGlyph> Rasterize(float px, const GlyphRun& run, const RasterizeOptions& options = {}) = 0;
	};
}
