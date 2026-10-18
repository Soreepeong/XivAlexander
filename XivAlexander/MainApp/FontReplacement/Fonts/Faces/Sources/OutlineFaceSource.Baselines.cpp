#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

#include <FontChanger.FixedSizeFont/opentype_positioning.h>
#include <FontChanger.FixedSizeFont/util.truetype.h>

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using namespace FontChanger::FixedSizeFont;

namespace {
	// In font units above the glyphs' origin (xivres read_baselines).
	std::map<uint32_t, int> ReadBaselines(IDWriteFontFace* face) {
		const void* data;
		UINT32 size;
		void* context;
		BOOL exists;
		if (FAILED(face->TryGetFontTable(truetype::Base::DirectoryTableTag.NativeValue, &data, &size, &context, &exists)) || !exists)
			return {};
		const auto release = xivres::util::on_dtor([&] { face->ReleaseFontTable(context); });
		return read_baselines(std::span(static_cast<const char*>(data), size));
	}
}

uint32_t FontReplacement::OutlineFaceSource::GetBaselineTag(Baseline baseline) {
	switch (baseline) {
		case Baseline::Roman:
			return truetype::Base::RomanBaselineTag.NativeValue;
		case Baseline::IdeographicBottom:
			return truetype::Base::IdeographicEmBoxBottomTag.NativeValue;
		case Baseline::IdeographicTop:
			return truetype::Base::IdeographicEmBoxTopTag.NativeValue;
		case Baseline::IdeographicFaceBottom:
			return truetype::Base::IdeographicFaceBottomTag.NativeValue;
		case Baseline::IdeographicFaceTop:
			return truetype::Base::IdeographicFaceTopTag.NativeValue;
	}
	return 0;
}

std::optional<float> FontReplacement::OutlineFaceSource::TryGetBaseline(float px, Baseline baseline) {
	if (!m_baselines)
		m_baselines = ReadBaselines(m_font->Face());
	const auto it = m_baselines->find(GetBaselineTag(baseline));
	if (it == m_baselines->end())
		return std::nullopt;
	return static_cast<float>(it->second) * px * m_transform.M22 / static_cast<float>(m_font->Metrics().designUnitsPerEm);
}
