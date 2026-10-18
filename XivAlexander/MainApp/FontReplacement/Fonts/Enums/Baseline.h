#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// The BASE table's baseline tags: https://learn.microsoft.com/en-us/typography/opentype/otspec181/baselinetags
	enum class Baseline : uint8_t {
		Roman, // romn
		IdeographicBottom, // ideo, of the em box
		IdeographicTop, // idtp, of the em box
		IdeographicFaceBottom, // icfb
		IdeographicFaceTop, // icft
	};
}
