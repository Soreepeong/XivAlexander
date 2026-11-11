#pragma once

#include "MainApp/FontReplacement/Fonts/Records/WatchList.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// What reading a request's sources came to.
	struct LoadReport {
		// The status line.
		std::string Status;

		// The files to watch from now on, nullopt to keep watching the same.
		std::optional<WatchList> Watch;
	};
}
