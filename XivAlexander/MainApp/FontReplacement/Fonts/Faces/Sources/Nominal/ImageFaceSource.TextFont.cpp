#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/ImageFaceSource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

// Glyph merging's texts drawn with the glyph images: one glyph per codepoint at whole pixels, with the files' kerning.
std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontReplacement::ImageFaceSource::CreateMergeTextFont(float size, const Matrix2x2& transform) {
	return GetFont(size, transform);
}
