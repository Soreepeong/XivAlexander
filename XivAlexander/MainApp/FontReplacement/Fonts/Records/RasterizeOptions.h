#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct RasterizeOptions {
		// The fraction of a pixel the pen is at.
		float OriginX = 0;

		// The advance the glyph is given; nullopt for its own, rounded.
		std::optional<int> Advance;

		// Narrowed by monospacing: outline fonts draw it so, the others resample it.
		float SqueezeX = 1;
	};
}
