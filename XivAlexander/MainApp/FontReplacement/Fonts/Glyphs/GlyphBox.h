#pragma once

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A raster fitted to what the game's glyphs can hold.
	// A glyph's width and height are bytes, and its offsets signed bytes. It has no left bearing, so its box starts at the pen or right of
	// it. A box is a full line high.
	struct GlyphBox {
		// The box's left relative to the pen, as the raster's is.
		int Left;

		// The box's top relative to the line's.
		int Top;

		int Width;
		int Height;

		// The ink's top row in the box.
		int InkTop;

		// What the glyph advances by past its width.
		int OffsetX;

		// Whether it has pixels to place.
		[[nodiscard]] bool HasBox() const { return Width != 0; }

		// How far the ink reaches left of the pen, where a box can't follow it. 0 if it doesn't, or if there is no ink.
		[[nodiscard]] static int InkLeftOfPen(const RasterGlyph& r) { return r.Width > 0 && r.Left < 0 ? -r.Left : 0; }

		// r is relative to the pen on the baseline.
		[[nodiscard]] static GlyphBox Fit(const RasterGlyph& r, int ascent, int lineHeight);

		void WriteTo(GameGlyph* glyph) const;
	};
}
