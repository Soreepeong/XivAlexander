#pragma once

#include <FontChanger.Presets/ElementFonts.h>

#include "MainApp/FontReplacement/GlyphRasterizer.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// As FontChanger.Presets.Native's ResolveFont and ResolveSynthesis; with explicit synthesis, the real face is taken and what it lacks is synthesized.
	class LookupFont {
		IDWriteFontPtr m_font;
		IDWriteFontFacePtr m_face;
		DWRITE_FONT_METRICS m_metrics{};
		FontChanger::ElementFonts::SynthesizedFace m_synthesis;
		DWRITE_FONT_SIMULATIONS m_simulations = DWRITE_FONT_SIMULATIONS_NONE;
		IDWriteFontResourcePtr m_resource;

		// The axis values, without the optical size when it follows the text size; and the faces made with them.
		std::vector<DWRITE_FONT_AXIS_VALUE> m_axes;
		std::map<float, IDWriteFontFacePtr> m_sizedFaces;
		DWRITE_FONT_AXIS_RANGE m_opticalSizeRange{};
		std::map<uint32_t, float> m_axisValues;
		bool m_autoOpticalSize = false;

		DWRITE_FONT_WEIGHT m_layoutWeight;
		DWRITE_FONT_STRETCH m_layoutStretch;
		DWRITE_FONT_STYLE m_layoutStyle;

		LookupFont(IDWriteFactory2* factory, IDWriteFontPtr font, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::RendererEnum renderer);

	public:
		// nullptr if the family isn't installed.
		static std::unique_ptr<LookupFont> Create(const GlyphRasterizer& rasterizer, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::RendererEnum renderer);

		[[nodiscard]] IDWriteFont* Font() const { return m_font; }

		// With the simulations, but for a variable font the matched instance (for its file and metrics); glyphs are drawn from GetFace's.
		[[nodiscard]] IDWriteFontFace* Face() const { return m_face; }

		[[nodiscard]] const DWRITE_FONT_METRICS& Metrics() const { return m_metrics; }

		[[nodiscard]] const FontChanger::ElementFonts::SynthesizedFace& Synthesis() const { return m_synthesis; }

		// The screen transform synthesis makes, before the element's: a slant (FreeType), a width.
		[[nodiscard]] GlyphTransform Transform() const { return GlyphTransform::Of(m_synthesis.GetScreenMatrix()); }

		// In ems (negative values thin); glyphs advance that much further.
		[[nodiscard]] float Embolden() const { return m_synthesis.GetEmbolden(); }

		// What text layouts ask for to match this font with these simulations.
		[[nodiscard]] DWRITE_FONT_WEIGHT LayoutWeight() const { return m_layoutWeight; }
		[[nodiscard]] DWRITE_FONT_STRETCH LayoutStretch() const { return m_layoutStretch; }
		[[nodiscard]] DWRITE_FONT_STYLE LayoutStyle() const { return m_layoutStyle; }

		// In the font's axis order; none if it isn't variable.
		[[nodiscard]] const std::vector<DWRITE_FONT_AXIS_VALUE>& LayoutAxes() const { return m_axes; }

		[[nodiscard]] bool AutoOpticalSize() const { return m_autoOpticalSize; }

		// The optical size is set to px if it follows the text size. Owned by this object.
		[[nodiscard]] IDWriteFontFace* GetFace(float px);

		// The values FreeType sets, keyed by DWRITE_FONT_AXIS_TAG.
		[[nodiscard]] std::map<uint32_t, float> GetAxisValues(float px) const;

		static IDWriteFontFacePtr WithSimulations(IDWriteFactory2* factory, IDWriteFontFace* face, DWRITE_FONT_SIMULATIONS simulations);

	private:
		// DirectWrite has variable fonts from Windows 10 21H1.
		[[nodiscard]] bool IsVariable() const { return !!m_resource; }

		static IDWriteFontPtr GetRealFont(IDWriteFactory2* factory, IDWriteFont* font);
	};
}
