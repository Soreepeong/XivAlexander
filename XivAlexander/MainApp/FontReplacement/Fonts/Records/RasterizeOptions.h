#pragma once

namespace XivAlexander::Apps::MainApp::FontReplacement {
	struct RasterizeOptions {
		// The pen's fraction of a pixel.
		float OriginX = 0;

		// nullopt keeps the glyph's own, rounded.
		std::optional<int> Advance;

		// Narrows for monospacing, which outline fonts draw so and the others resample.
		float SqueezeX = 1;
	};
}
