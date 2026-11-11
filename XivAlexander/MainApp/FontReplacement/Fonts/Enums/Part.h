#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A part that turns itself off when it fails, until font replacement is set up again.
	enum class Part {
		Replacer,
		Subpixel,
	};
}
