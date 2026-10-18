#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The BASE table baselines a font aligns its elements by.
	// https://learn.microsoft.com/en-us/typography/opentype/otspec181/baselinetags
	enum class Baseline : uint8_t {
		// romn
		Roman,

		// ideo: bottom of the ideographic em box.
		IdeographicBottom,

		// idtp: top of the ideographic em box.
		IdeographicTop,

		// icfb: bottom of the ideographic character face.
		IdeographicFaceBottom,

		// icft: top of the ideographic character face.
		IdeographicFaceTop,
	};
}
