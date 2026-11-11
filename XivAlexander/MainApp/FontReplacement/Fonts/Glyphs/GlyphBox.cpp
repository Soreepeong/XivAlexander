#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphBox.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::GlyphBox FontReplacement::GlyphBox::Fit(const RasterGlyph& r, int ascent, int lineHeight) {
	// The game has no left bearing, so overhanging ink moves right into the box. Past 255 wide, it is cut on the right.
	const auto left = (std::min)(r.Left, 0);
	const auto width = r.Width == 0 ? 0 : (std::min)(r.Left + r.Width - left, 255);

	// A box is a full line high, as the game's are, so that italics slant alike. Ink beyond the line grows the box.
	const auto inkTop = ascent + r.Top;
	auto top = 0;
	auto height = lineHeight;
	if (width != 0) {
		top = std::clamp(inkTop, -128, 0);
		height = (std::min)((std::max)(lineHeight, inkTop + r.Height) - top, 255);
	}

	return {left, top, width, height, inkTop - top, std::clamp(r.Advance - width, -128, 127)};
}

void FontReplacement::GlyphBox::WriteTo(GameGlyph* glyph) const {
	glyph->Width = static_cast<uint8_t>(Width);
	glyph->Height = static_cast<uint8_t>(Height);
	glyph->OffsetX = static_cast<int8_t>(OffsetX);
	glyph->OffsetY = static_cast<int8_t>(Top);
}
