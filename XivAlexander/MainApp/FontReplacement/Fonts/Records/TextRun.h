#pragma once

#include "MainApp/FontReplacement/Fonts/Records/ScriptToken.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct TextRun {
		int Start = 0;
		int End = 0;
		ScriptToken Script;
		bool RightToLeft = false;
	};
}
