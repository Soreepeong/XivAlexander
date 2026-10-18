#pragma once

#include <dwrite_3.h>

#include <FontChanger.Presets/ElementFonts.h>

#include "MainApp/FontReplacement/Fonts/Rasterization/DirectWriteGlyphRasterizer.h"

_COM_SMARTPTR_TYPEDEF(IDWriteFont3, __uuidof(IDWriteFont3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace5, __uuidof(IDWriteFontFace5));
_COM_SMARTPTR_TYPEDEF(IDWriteFontResource, __uuidof(IDWriteFontResource));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	// As FontChanger's ResolveFont and ResolveSynthesis: explicit synthesis starts from the real face and synthesizes what it lacks.
	class LookupFont {
		IDWriteFont3Ptr m_font;
		IDWriteFontFace5Ptr m_face;
		DWRITE_FONT_METRICS m_metrics{};
		Matrix2x2 m_transform;
		float m_embolden = 0;
		DWRITE_FONT_SIMULATIONS m_simulations = DWRITE_FONT_SIMULATIONS_NONE;
		IDWriteFontResourcePtr m_resource;

		// Without the optical size when it follows the text size, which GetFace adds per size.
		std::vector<DWRITE_FONT_AXIS_VALUE> m_axes;
		std::map<float, IDWriteFontFace5Ptr> m_sizedFaces;
		DWRITE_FONT_AXIS_RANGE m_opticalSizeRange{};
		bool m_autoOpticalSize = false;

		// Keyed by DWRITE_FONT_AXIS_TAG, without the optical size when it follows the text size.
		std::map<uint32_t, float> m_axisValues;

		LookupFont(IDWriteFont3Ptr font, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::ElementSourceEnum renderer);

	public:
		LookupFont(const LookupFont&) = delete;
		LookupFont& operator=(const LookupFont&) = delete;

		// nullptr if the family isn't installed.
		static std::unique_ptr<LookupFont> Create(FontServices& services, const FontChanger::Structs::LookupStruct& lookup, FontChanger::Structs::ElementSourceEnum renderer);

		// For a variable font, the matched instance, used for its file and metrics; glyphs are drawn from GetFace's.
		[[nodiscard]] IDWriteFontFace5* Face() const { return m_face; }

		[[nodiscard]] const DWRITE_FONT_METRICS& Metrics() const { return m_metrics; }

		// Synthesis's slant (FreeType) and width on screen, applied before the element's transformation.
		[[nodiscard]] const Matrix2x2& Transform() const { return m_transform; }

		// In ems (negative values thin glyphs); glyphs advance that much further.
		[[nodiscard]] float Embolden() const { return m_embolden; }

		// Owned by this object.
		[[nodiscard]] IDWriteFontFace5* GetFace(float px);

		// For FreeType, keyed by DWRITE_FONT_AXIS_TAG.
		[[nodiscard]] std::map<uint32_t, float> GetAxisValues(float px) const;

	private:
		// DirectWrite has variable fonts from Windows 10 21H1.
		[[nodiscard]] bool IsVariable() const { return !!m_resource; }

		static IDWriteFontFace5Ptr WithSimulations(IDWriteFontFace5* face, DWRITE_FONT_SIMULATIONS simulations);

		// The font without simulations a simulated system font is made from.
		static IDWriteFont3Ptr GetRealFont(IDWriteFont3* font);
	};
}
