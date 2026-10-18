#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/MergingFaceSource.h"

#include <FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/Nominal/ImageFaceSource.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/OutlineFaceSource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::FontElement;
using FontChanger::Structs::ElementSourceEnum;

FontReplacement::MergingFaceSource::MergingFaceSource(std::unique_ptr<IFaceSource> inner, FontElement def, FontServices& services, std::unique_ptr<OutlineFaceSource> ownTextOutline)
	: m_inner(std::move(inner))
	, m_def(std::move(def))
	, m_services(services)
	, m_ownTextOutline(std::move(ownTextOutline))
	, m_textOutline(m_ownTextOutline ? m_ownTextOutline.get() : dynamic_cast<OutlineFaceSource*>(m_inner.get())) {
	for (const auto& mapping : m_def.GlyphMerging.Params.Mappings) {
		for (size_t i = 0; i < mapping.Codepoints.size() && i < mapping.Texts.size(); i++) {
			if (!mapping.Texts[i].empty())
				m_codepoints.insert(mapping.Codepoints[i]);
		}
	}
}

FontReplacement::MergingFaceSource::~MergingFaceSource() = default;

std::unique_ptr<FontReplacement::MergingFaceSource> FontReplacement::MergingFaceSource::Create(std::unique_ptr<IFaceSource> inner, const FontElement& def, FontServices& services, const std::string& fontName) {
	std::unique_ptr<OutlineFaceSource> ownTextOutline;
	if (dynamic_cast<ImageFaceSource*>(inner.get()) && !def.Lookup.FamilyName.empty()) {
		auto textDef = def;
		textDef.Source = ElementSourceEnum::DirectWrite;
		ownTextOutline = OutlineFaceSource::Create(services, textDef, fontName);
	}
	return std::unique_ptr<MergingFaceSource>(new MergingFaceSource(std::move(inner), def, services, std::move(ownTextOutline)));
}

std::vector<FontReplacement::OutlineFaceSource*> FontReplacement::MergingFaceSource::OutlineSources() const {
	std::vector<OutlineFaceSource*> res;
	if (const auto outline = dynamic_cast<OutlineFaceSource*>(m_inner.get()))
		res.push_back(outline);
	if (m_ownTextOutline)
		res.push_back(m_ownTextOutline.get());
	return res;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::MergingFaceSource::DrawInner(char32_t codepoint, float px) {
	const auto id = m_inner->GetGlyphId(codepoint);
	if (!id)
		return std::nullopt;
	const uint32_t ids[]{id};
	const float advances[]{m_inner->GetAdvance(px, id)};
	return m_inner->Rasterize(px, {ids, advances});
}
