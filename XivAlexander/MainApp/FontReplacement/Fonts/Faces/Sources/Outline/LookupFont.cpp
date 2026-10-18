#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Faces/Sources/Outline/LookupFont.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;
using FontChanger::Structs::LookupStruct;
using FontChanger::Structs::ElementSourceEnum;

FontReplacement::LookupFont::LookupFont(IDWriteFont3Ptr font, const LookupStruct& lookup, ElementSourceEnum renderer)
	: m_font(std::move(font)) {
	IDWriteFontFace3Ptr face3;
	ThrowOnError(m_font->CreateFontFace(&face3), "CreateFontFace");
	ThrowOnError(face3.QueryInterface(__uuidof(IDWriteFontFace5), &m_face), "QueryInterface(IDWriteFontFace5)");
	m_face->GetMetrics(&m_metrics);

	// A variable font starts from its instance (DirectWrite lists the named ones as fonts), less the optical size.
	std::vector<DWRITE_FONT_AXIS_RANGE> ranges;
	if (m_face->HasVariations()) {
		ThrowOnError(m_face->GetFontResource(&m_resource), "GetFontResource");
		ranges.resize(m_resource->GetFontAxisCount());
		ThrowOnError(m_resource->GetFontAxisRanges(ranges.data(), static_cast<UINT32>(ranges.size())), "GetFontAxisRanges");
		m_axes.resize(m_face->GetFontAxisValueCount());
		ThrowOnError(m_face->GetFontAxisValues(m_axes.data(), static_cast<UINT32>(m_axes.size())), "GetFontAxisValues");
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

		// The face of the font comes with the font's simulations; one with others is made from its resource.
		if (m_simulations != m_face->GetSimulations())
			m_face = WithSimulations(m_face, m_simulations);
	}
}

std::unique_ptr<FontReplacement::LookupFont> FontReplacement::LookupFont::Create(FontServices& services, const LookupStruct& lookup, ElementSourceEnum renderer) {
	IDWriteFont3Ptr font;
	if (!services.TryFindFont(lookup, font))
		return nullptr;

	// DirectWrite may match a simulated font: with explicit synthesis, the real face it is made from is taken.
	if (lookup.Synthesis && font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
		font = GetRealFont(font);

	return std::unique_ptr<LookupFont>(new LookupFont(std::move(font), lookup, renderer));
}

IDWriteFontFace5* FontReplacement::LookupFont::GetFace(float px) {
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

IDWriteFontFace5Ptr FontReplacement::LookupFont::WithSimulations(IDWriteFontFace5* face, DWRITE_FONT_SIMULATIONS simulations) {
	IDWriteFontResourcePtr resource;
	ThrowOnError(face->GetFontResource(&resource), "GetFontResource");
	std::vector<DWRITE_FONT_AXIS_VALUE> axes(face->GetFontAxisValueCount());
	ThrowOnError(face->GetFontAxisValues(axes.data(), static_cast<UINT32>(axes.size())), "GetFontAxisValues");
	IDWriteFontFace5Ptr made;
	ThrowOnError(resource->CreateFontFace(simulations, axes.data(), static_cast<UINT32>(axes.size()), &made), "CreateFontFace");
	return made;
}

IDWriteFont3Ptr FontReplacement::LookupFont::GetRealFont(IDWriteFont3* font) {
	IDWriteFontFace3Ptr face3;
	ThrowOnError(font->CreateFontFace(&face3), "CreateFontFace");
	IDWriteFontFace5Ptr face;
	ThrowOnError(face3.QueryInterface(__uuidof(IDWriteFontFace5), &face), "QueryInterface(IDWriteFontFace5)");
	const auto realFace = WithSimulations(face, DWRITE_FONT_SIMULATIONS_NONE);
	IDWriteFontFamilyPtr family;
	ThrowOnError(font->GetFontFamily(&family), "GetFontFamily");
	IDWriteFontCollectionPtr collection;
	ThrowOnError(family->GetFontCollection(&collection), "GetFontCollection");
	IDWriteFontPtr real;
	ThrowOnError(collection->GetFontFromFontFace(realFace, &real), "GetFontFromFontFace");
	IDWriteFont3Ptr real3;
	ThrowOnError(real.QueryInterface(__uuidof(IDWriteFont3), &real3), "QueryInterface(IDWriteFont3)");
	return real3;
}
