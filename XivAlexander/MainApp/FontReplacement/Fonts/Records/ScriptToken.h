#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A script as the analyzer found it, opaque but to the shaping that pairs with it (DWRITE_SCRIPT_ANALYSIS).
	struct ScriptToken {
		uint16_t Script = 0;
		int Shapes = 0;

		bool operator==(const ScriptToken&) const = default;
	};
}
