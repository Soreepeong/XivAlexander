#pragma once

#include <FontChanger.FixedSizeFont/merged_fixed_size_font.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Where a font's line is at a size, for elements aligned to it.
	struct FontAlignment {
		int Ascent = 0;
		int LineHeight = 0;
		float RomanBaselineY = 0;
		float IdeographicCenterY = 0;
		FontChanger::FixedSizeFont::vertical_alignment Mode = FontChanger::FixedSizeFont::vertical_alignment::Baseline;
	};
}
