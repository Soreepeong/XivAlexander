#pragma once

#include <dwrite_3.h>

#include <FontChanger.Presets/ElementFonts.h>

#include "MainApp/FontReplacement/Fonts/Rasterization/DirectWriteGlyphRasterizer.h"

_COM_SMARTPTR_TYPEDEF(IDWriteFont3, __uuidof(IDWriteFont3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace5, __uuidof(IDWriteFontFace5));
_COM_SMARTPTR_TYPEDEF(IDWriteFontResource, __uuidof(IDWriteFontResource));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	// The font a lookup resolves to, as FontChanger makes it (ResolveFont, ResolveSynthesis): the face DirectWrite matches, its simulations, and a
	// variable font's axis values. With explicit synthesis, the real face is taken instead and what the lookup asks for that it lacks is made.
	class LookupFont {
		IDWriteFont3Ptr m_font;
		IDWriteFontFace5Ptr m_face;
		DWRITE_FONT_METRICS m_metrics{};
		FontChanger::ElementFonts::SynthesizedFace m_synthesis;
		DWRITE_FONT_SIMULATIONS m_simulations = DWRITE_FONT_SIMULATIONS_NONE;
		IDWriteFontResourcePtr m_resource;

		// The axis values, without the optical size when it follows the text size; and the faces made with them.
		std::vector<DWRITE_FONT_AXIS_VALUE> m_axes;
		std::map<float, IDWriteFontFace5Ptr> m_sizedFaces;
		DWRITE_FONT_AXIS_RANGE m_opticalSizeRange{};
		std::map<uint32_t, float> m_axisValues;
		bool m_autoOpticalSize = false;

		LookupFont(IDWriteFont3Ptr font, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::ElementSourceEnum renderer);

	public:
		LookupFont(const LookupFont&) = delete;
		LookupFont& operator=(const LookupFont&) = delete;

		// nullptr if the family isn't installed.
		static std::unique_ptr<LookupFont> Create(FontServices& services, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::ElementSourceEnum renderer);

		// With the simulations, but for a variable font the matched instance (for its file and metrics); glyphs are drawn from GetFace's.
		[[nodiscard]] IDWriteFontFace5* Face() const { return m_face; }

		[[nodiscard]] const DWRITE_FONT_METRICS& Metrics() const { return m_metrics; }

		// The screen transform synthesis makes, before the element's: a slant (FreeType), a width.
		[[nodiscard]] Matrix2x2 Transform() const { return Matrix2x2::Of(m_synthesis.GetScreenMatrix()); }

		// In ems (negative values thin); glyphs advance that much further.
		[[nodiscard]] float Embolden() const { return m_synthesis.GetEmbolden(); }

		[[nodiscard]] bool AutoOpticalSize() const { return m_autoOpticalSize; }

		// The optical size is set to px if it follows the text size. Owned by this object.
		[[nodiscard]] IDWriteFontFace5* GetFace(float px);

		// The values FreeType sets, keyed by DWRITE_FONT_AXIS_TAG.
		[[nodiscard]] std::map<uint32_t, float> GetAxisValues(float px) const;

	private:
		// DirectWrite has variable fonts from Windows 10 21H1.
		[[nodiscard]] bool IsVariable() const { return !!m_resource; }

		// Same font resource and axis values, other simulations.
		static IDWriteFontFace5Ptr WithSimulations(IDWriteFontFace5* face, DWRITE_FONT_SIMULATIONS simulations);

		// The font without simulations a simulated system font is made from.
		static IDWriteFont3Ptr GetRealFont(IDWriteFont3* font);
	};
}
