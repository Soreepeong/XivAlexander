#pragma once

#include <FontChanger.Presets/ElementFonts.h>

#include "MainApp/FontReplacement/GlyphRasterizer.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The font a lookup resolves to, as FontChanger makes it (FontChanger.Presets.Native's ResolveFont and
	// ResolveSynthesis): the face DirectWrite matches, its simulations, and the axis values of a variable font. With explicit
	// synthesis, the real face is taken instead, and what the lookup asks for that it lacks is made (SynthesizedFace).
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
		// Finds the font a lookup asks for and makes it as the element draws it; nullptr if the family isn't installed.
		static std::unique_ptr<LookupFont> Create(const GlyphRasterizer& rasterizer, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::RendererEnum renderer);

		[[nodiscard]] IDWriteFont* Font() const { return m_font; }

		// Gets the face: with the simulations, but for a variable font, the matched instance (for its file and metrics;
		// glyphs are drawn from GetFace's).
		[[nodiscard]] IDWriteFontFace* Face() const { return m_face; }

		[[nodiscard]] const DWRITE_FONT_METRICS& Metrics() const { return m_metrics; }

		// Gets what synthesis makes of the face.
		[[nodiscard]] const FontChanger::ElementFonts::SynthesizedFace& Synthesis() const { return m_synthesis; }

		// Gets the transformation on screen that synthesis makes, before the element's: a slant (FreeType), a width.
		[[nodiscard]] GlyphTransform Transform() const { return GlyphTransform::Of(m_synthesis.GetScreenMatrix()); }

		// Gets how much FreeType emboldens glyphs, in ems (negative values thin them); they advance that much further.
		[[nodiscard]] float Embolden() const { return m_synthesis.GetEmbolden(); }

		// Gets the properties text layouts ask for, which match this font with these simulations.
		[[nodiscard]] DWRITE_FONT_WEIGHT LayoutWeight() const { return m_layoutWeight; }
		[[nodiscard]] DWRITE_FONT_STRETCH LayoutStretch() const { return m_layoutStretch; }
		[[nodiscard]] DWRITE_FONT_STYLE LayoutStyle() const { return m_layoutStyle; }

		// Gets the axis values text layouts set, in the font's order (none if it isn't variable).
		[[nodiscard]] const std::vector<DWRITE_FONT_AXIS_VALUE>& LayoutAxes() const { return m_axes; }

		// Gets whether the optical size follows the text size.
		[[nodiscard]] bool AutoOpticalSize() const { return m_autoOpticalSize; }

		// Gets the face at a text size: the optical size set to it, if it follows. Owned by this object.
		[[nodiscard]] IDWriteFontFace* GetFace(float px);

		// Gets the axis values FreeType sets at a text size (as DWRITE_FONT_AXIS_TAG).
		[[nodiscard]] std::map<uint32_t, float> GetAxisValues(float px) const;

		// Makes a face of the same file and index as a face, with other simulations.
		static IDWriteFontFacePtr WithSimulations(IDWriteFactory2* factory, IDWriteFontFace* face, DWRITE_FONT_SIMULATIONS simulations);

	private:
		// Gets whether the font is variable (DirectWrite has them from Windows 10 21H1).
		[[nodiscard]] bool IsVariable() const { return !!m_resource; }

		// Gets the font without simulations a simulated font is made from.
		static IDWriteFontPtr GetRealFont(IDWriteFactory2* factory, IDWriteFont* font);
	};
}
