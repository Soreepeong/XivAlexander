#pragma once

#include "MainApp/FontReplacement/Fonts/Records/ScriptToken.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A run of a line: its code units, its script, and whether it is written right to left.
	struct TextRun {
		int Start = 0;
		int End = 0;
		ScriptToken Script;
		bool RightToLeft = false;
	};
}
