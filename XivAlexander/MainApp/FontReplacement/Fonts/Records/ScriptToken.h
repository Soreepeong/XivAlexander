#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A DWRITE_SCRIPT_ANALYSIS, opaque but to the shaping that pairs with it.
	struct ScriptToken {
		uint16_t Script = 0;
		int Shapes = 0;

		bool operator==(const ScriptToken&) const = default;
	};
}
