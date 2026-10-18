#pragma once

#include <FontChanger.FixedSizeFont/merged_fixed_size_font.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct FontAlignment {
		int Ascent = 0;
		int LineHeight = 0;
		float RomanBaselineY = 0;
		float IdeographicCenterY = 0;
		FontChanger::FixedSizeFont::vertical_alignment Mode = FontChanger::FixedSizeFont::vertical_alignment::Baseline;
	};
}
