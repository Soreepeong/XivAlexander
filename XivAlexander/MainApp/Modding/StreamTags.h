#pragma once

#include <string>

namespace XivAlexander::Apps::MainApp::Features::Modding {
	/// On a modpack's data file, so that anything read from it can be named.
	struct ModpackNameTag {
		std::string Value;
	};

	/// On data made in memory, saying what it is.
	struct SourceNoteTag {
		std::string Value;
	};
}
