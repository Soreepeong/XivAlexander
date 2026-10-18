#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontServices.h"

#include "MainApp/FontReplacement/Fonts/Text/DirectWriteTextAnalyzer.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontServices::FontServices() {
	ThrowOnError(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory7), reinterpret_cast<IUnknown**>(&m_factory)), "DWriteCreateFactory");
	ThrowOnError(m_factory->CreateTextAnalyzer(&m_analyzer), "CreateTextAnalyzer");

	IDWriteFontCollectionPtr systemFonts;
	ThrowOnError(m_factory->GetSystemFontCollection(&systemFonts, FALSE), "GetSystemFontCollection");
	ThrowOnError(systemFonts.QueryInterface(__uuidof(IDWriteFontCollection3), &m_systemFonts), "IDWriteFontCollection3");

	m_textAnalyzer = std::make_unique<DirectWriteTextAnalyzer>(*this);
	m_freeType = std::make_unique<FreeTypeGlyphRasterizer>();
	m_directWrite = std::make_unique<DirectWriteGlyphRasterizer>(m_factory.GetInterfacePtr(), *m_freeType);
}

FontReplacement::FontServices::~FontServices() = default;

void FontReplacement::FontServices::SetSubpixelOrder(SubpixelOrder value) {
	m_subpixelOrder = value;
	m_directWrite->SetSubpixelOrder(value);
	m_freeType->SetSubpixelOrder(value);
}

bool FontReplacement::FontServices::TryFindFont(const FontChanger::Structs::LookupStruct& lookup, IDWriteFont3Ptr& match) const {
	match = nullptr;
	const auto name = xivres::util::unicode::convert<std::wstring>(lookup.FamilyName);
	UINT32 index;
	BOOL exists;
	if (FAILED(m_systemFonts->FindFamilyName(name.c_str(), &index, &exists)) || !exists)
		return false;

	IDWriteFontFamily2Ptr family;
	if (FAILED(m_systemFonts->GetFontFamily(index, &family)))
		return false;
	IDWriteFont3Ptr font;
	if (FAILED(GetFirstMatchingFont(family, lookup.Weight, lookup.Stretch, lookup.Style, &font)))
		return false;
	match = std::move(font);
	return true;
}
