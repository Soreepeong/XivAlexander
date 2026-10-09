#include "pch.h"
#include "MainApp/FontReplacement/LookupFont.h"

#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::LookupStruct;
using FontChanger::Structs::RendererEnum;

FontReplacement::LookupFont::LookupFont(IDWriteFactory2* factory, IDWriteFontPtr font, const LookupStruct& lookup, RendererEnum renderer)
	: m_font(std::move(font)) {
	ThrowOnError(m_font->CreateFontFace(&m_face), "CreateFontFace");
	m_face->GetMetrics(&m_metrics);

	// A variable font starts from its instance (DirectWrite lists the named ones as fonts), less the optical size.
	std::vector<DWRITE_FONT_AXIS_RANGE> ranges;
	if (IDWriteFontFace5Ptr face5; SUCCEEDED(m_face.QueryInterface(__uuidof(IDWriteFontFace5), &face5)) && face5->HasVariations()) {
		ThrowOnError(face5->GetFontResource(&m_resource), "GetFontResource");
		ranges.resize(m_resource->GetFontAxisCount());
		ThrowOnError(m_resource->GetFontAxisRanges(ranges.data(), static_cast<UINT32>(ranges.size())), "GetFontAxisRanges");
		m_axes.resize(face5->GetFontAxisValueCount());
		ThrowOnError(face5->GetFontAxisValues(m_axes.data(), static_cast<UINT32>(m_axes.size())), "GetFontAxisValues");
	}

	std::map<uint32_t, float> axisValues;
	for (const auto& a : m_axes) {
		if (a.axisTag != DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE)
			axisValues[static_cast<uint32_t>(a.axisTag)] = a.value;
	}

	m_synthesis = FontChanger::ElementFonts::ResolveSynthesis(lookup, renderer, m_font);
	m_simulations = m_synthesis.Simulations.value_or(m_font->GetSimulations());
	for (const auto& [tag, value] : m_synthesis.AxisValues)
		axisValues[tag] = value;
	for (const auto& [tag, value] : lookup.GetVariationAxisValues())
		axisValues[tag] = value;
	m_axisValues = axisValues;

	// Layouts match this font again by the properties that make DirectWrite apply the same simulations.
	const auto bold = (m_simulations & DWRITE_FONT_SIMULATIONS_BOLD) != 0;
	const auto oblique = (m_simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0;
	if (!m_synthesis.Simulations) {
		m_layoutWeight = lookup.Weight;
		m_layoutStretch = lookup.Stretch;
		m_layoutStyle = lookup.Style;
	} else {
		m_layoutWeight = bold ? (std::max)(m_font->GetWeight(), DWRITE_FONT_WEIGHT_BOLD) : m_font->GetWeight();
		m_layoutStretch = m_font->GetStretch();
		m_layoutStyle = oblique ? DWRITE_FONT_STYLE_OBLIQUE : m_font->GetStyle();
	}

	if (IsVariable()) {
		// Unless set, the optical size follows the text size.
		std::vector<DWRITE_FONT_AXIS_VALUE> list;
		for (const auto& range : ranges) {
			if (const auto it = axisValues.find(static_cast<uint32_t>(range.axisTag)); it != axisValues.end())
				list.push_back({range.axisTag, std::clamp(it->second, range.minValue, range.maxValue)});
			else if (range.axisTag == DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE)
				m_autoOpticalSize = true;
		}

		m_axes = std::move(list);
		if (const auto it = std::ranges::find(ranges, DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, &DWRITE_FONT_AXIS_RANGE::axisTag); it != ranges.end())
			m_opticalSizeRange = *it;
	} else {
		m_axes.clear();
		if (m_simulations != m_face->GetSimulations()) {
			// The face of the font comes with the font's simulations; one with others is made from the file.
			m_face = WithSimulations(factory, m_face, m_simulations);
		}
	}
}

std::unique_ptr<FontReplacement::LookupFont> FontReplacement::LookupFont::Create(const GlyphRasterizer& rasterizer, const LookupStruct& lookup, RendererEnum renderer) {
	auto font = rasterizer.FindFont(lookup.Name, lookup.Weight, lookup.Stretch, lookup.Style);
	if (!font)
		return nullptr;

	// DirectWrite may match a simulated font: with explicit synthesis, the real face it is made from is taken.
	if (lookup.Synthesis && font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
		font = GetRealFont(rasterizer.Factory(), font);

	return std::unique_ptr<LookupFont>(new LookupFont(rasterizer.Factory(), std::move(font), lookup, renderer));
}

IDWriteFontFace* FontReplacement::LookupFont::GetFace(float px) {
	if (!IsVariable())
		return m_face;

	const auto key = m_autoOpticalSize ? std::round(px * 4) / 4 : 0.f;
	if (const auto it = m_sizedFaces.find(key); it != m_sizedFaces.end())
		return it->second;

	auto values = m_axes;
	if (m_autoOpticalSize)
		values.push_back({DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, std::clamp(key, m_opticalSizeRange.minValue, m_opticalSizeRange.maxValue)});

	IDWriteFontFace5Ptr face;
	ThrowOnError(m_resource->CreateFontFace(m_simulations, values.data(), static_cast<UINT32>(values.size()), &face), "CreateFontFace");
	return m_sizedFaces.emplace(key, face).first->second;
}

std::map<uint32_t, float> FontReplacement::LookupFont::GetAxisValues(float px) const {
	if (!m_autoOpticalSize)
		return m_axisValues;
	auto res = m_axisValues;
	res[static_cast<uint32_t>(DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE)] = px;
	return res;
}

IDWriteFontFacePtr FontReplacement::LookupFont::WithSimulations(IDWriteFactory2* factory, IDWriteFontFace* face, DWRITE_FONT_SIMULATIONS simulations) {
	UINT32 count = 1;
	IDWriteFontFilePtr file;
	ThrowOnError(face->GetFiles(&count, &file), "GetFiles");
	IDWriteFontFile* files[]{file};
	IDWriteFontFacePtr made;
	ThrowOnError(factory->CreateFontFace(face->GetType(), 1, files, face->GetIndex(), simulations, &made), "CreateFontFace");
	return made;
}

IDWriteFontPtr FontReplacement::LookupFont::GetRealFont(IDWriteFactory2* factory, IDWriteFont* font) {
	IDWriteFontFacePtr face;
	ThrowOnError(font->CreateFontFace(&face), "CreateFontFace");
	const auto realFace = WithSimulations(factory, face, DWRITE_FONT_SIMULATIONS_NONE);
	IDWriteFontFamilyPtr family;
	ThrowOnError(font->GetFontFamily(&family), "GetFontFamily");
	IDWriteFontCollectionPtr collection;
	ThrowOnError(family->GetFontCollection(&collection), "GetFontCollection");
	IDWriteFontPtr real;
	ThrowOnError(collection->GetFontFromFontFace(realFace, &real), "GetFontFromFontFace");
	return real;
}
