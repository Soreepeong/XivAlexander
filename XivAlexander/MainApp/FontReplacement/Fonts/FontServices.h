#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Enums/SubpixelOrder.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/DirectWriteGlyphRasterizer.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/FreeTypeGlyphRasterizer.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class DirectWriteTextAnalyzer;

	// The DirectWrite factory, the system's fonts, and what draws and analyzes text with them.
	class FontServices {
		IDWriteFactory7Ptr m_factory;
		IDWriteFontCollection3Ptr m_systemFonts;
		IDWriteTextAnalyzerPtr m_analyzer;
		std::unique_ptr<DirectWriteTextAnalyzer> m_textAnalyzer;
		std::unique_ptr<FreeTypeGlyphRasterizer> m_freeType;
		std::unique_ptr<DirectWriteGlyphRasterizer> m_directWrite;
		SubpixelOrder m_subpixelOrder = SubpixelOrder::None;

	public:
		FontServices();
		FontServices(const FontServices&) = delete;
		FontServices& operator=(const FontServices&) = delete;
		~FontServices();

		[[nodiscard]] IDWriteFactory7& Factory() const { return *m_factory; }

		[[nodiscard]] IDWriteTextAnalyzer& Analyzer() const { return *m_analyzer; }

		// Weight, stretch and style families, as the newer overloads (which take a family model) don't make by default.
		[[nodiscard]] IDWriteFontCollection3& SystemCollection() const { return *m_systemFonts; }

		[[nodiscard]] DirectWriteTextAnalyzer& TextAnalyzer() const { return *m_textAnalyzer; }

		[[nodiscard]] DirectWriteGlyphRasterizer& DirectWrite() const { return *m_directWrite; }

		[[nodiscard]] FreeTypeGlyphRasterizer& FreeType() const { return *m_freeType; }

		// Both rasterizers'. Game thread.
		[[nodiscard]] SubpixelOrder GetSubpixelOrder() const { return m_subpixelOrder; }
		void SetSubpixelOrder(SubpixelOrder value);

		// The family's font closest to the weight, stretch and style (simulated if the family lacks it); false if the family isn't installed.
		bool TryFindFont(const FontChanger::Structs::LookupStruct& lookup, IDWriteFont3Ptr& match) const;
	};
}
