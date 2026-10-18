#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/MergingFaceSource.h"

#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/ImageFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

// The fonts merged glyphs' texts are drawn with, which the element's outline or image source makes (as fixed_size_font, glyph merging's text font).
std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> FontReplacement::MergingFaceSource::CreateTextFont(float size, float condense) {
	const auto transform = Matrix2x2{condense, 0, 0, 1} * Matrix2x2::Of(m_def.GlyphMerging.TextTransform) * Matrix2x2::Of(m_def.Transform);
	if (m_textOutline)
		return m_textOutline->CreateMergeTextFont(size, transform);
	if (const auto images = dynamic_cast<ImageFaceSource*>(m_inner.get()))
		return images->CreateMergeTextFont(size, transform);
	return nullptr;
}
