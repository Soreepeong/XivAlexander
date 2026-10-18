#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/SystemFallbackFonts.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/Text/TextAnalysisSource.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

_COM_SMARTPTR_TYPEDEF(IDWriteFontFallbackBuilder, __uuidof(IDWriteFontFallbackBuilder));

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// For private-use characters, which the system's fallback would draw with Segoe Fluent Icons.
	constexpr auto IconFamily = L"XIV AXIS Std ATK";

	// Segoe UI, then fonts for scripts it lacks.
	constexpr const wchar_t* UiFamilies[]{
		L"Segoe UI", L"Yu Gothic UI", L"Malgun Gothic", L"Microsoft YaHei UI", L"Microsoft JhengHei UI", L"Nirmala UI", L"Leelawadee UI",
		L"Segoe UI Symbol", L"Segoe UI Emoji",
	};

	FontChanger::Structs::LookupStruct LookupOf(const wchar_t* family) {
		FontChanger::Structs::LookupStruct lookup;
		lookup.FamilyName = xivres::util::unicode::convert<std::string>(std::wstring_view(family));
		return lookup;
	}

	std::wstring GetFamilyName(IDWriteFont* font) {
		IDWriteFontFamilyPtr family;
		IDWriteLocalizedStringsPtr names;
		if (FAILED(font->GetFontFamily(&family)) || FAILED(family->GetFamilyNames(&names)) || names->GetCount() == 0)
			return {};

		UINT32 index;
		BOOL exists;
		if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists)
			index = 0;

		UINT32 length;
		if (FAILED(names->GetStringLength(index, &length)))
			return {};
		std::wstring name(length, L'\0');
		return SUCCEEDED(names->GetString(index, name.data(), length + 1)) ? name : std::wstring();
	}
}

FontReplacement::SystemFallbackFonts::SystemFallbackFonts(FontServices& services)
	: m_services(services) {
	IDWriteFontFallbackBuilderPtr builder;
	ThrowOnError(services.Factory().CreateFontFallbackBuilder(&builder), "CreateFontFallbackBuilder");

	const auto installed = [&services](const wchar_t* family) {
		IDWriteFont3Ptr font;
		return services.TryFindFont(LookupOf(family), font);
	};

	if (installed(IconFamily)) {
		const DWRITE_UNICODE_RANGE pua{0xE000, 0xF8FF};
		const wchar_t* names[]{IconFamily};
		ThrowOnError(builder->AddMapping(&pua, 1, names, 1, nullptr, nullptr, nullptr, 1), "AddMapping");
	} else {
		Host::Warning("{} isn't installed; private-use characters are drawn with the system's fallback", xivres::util::unicode::convert<std::string>(std::wstring_view(IconFamily)));
	}

	std::vector<const wchar_t*> ui;
	std::ranges::copy_if(UiFamilies, std::back_inserter(ui), installed);
	if (!ui.empty()) {
		const DWRITE_UNICODE_RANGE all{0, 0x10FFFF};
		ThrowOnError(builder->AddMapping(&all, 1, ui.data(), static_cast<UINT32>(ui.size()), nullptr, nullptr, nullptr, 1), "AddMapping");
	}

	IDWriteFontFallbackPtr systemFallback;
	ThrowOnError(services.Factory().GetSystemFontFallback(&systemFallback), "GetSystemFontFallback");
	ThrowOnError(builder->AddMappings(systemFallback), "AddMappings");
	ThrowOnError(builder->CreateFontFallback(&m_fallback), "CreateFontFallback");
}

std::optional<std::pair<FontChanger::Structs::LookupStruct, float>> FontReplacement::SystemFallbackFonts::Find(std::wstring_view text, std::string_view baseFamily) {
	if (text.empty())
		return std::nullopt;

	UINT32 mapped = 0;
	float scale = 1;
	IDWriteFontPtr font;
	TextAnalysisSource source(text.data(), static_cast<UINT32>(text.size()));
	const auto family = xivres::util::unicode::convert<std::wstring>(baseFamily);
	ThrowOnError(m_fallback->MapCharacters(
		&source,
		0,
		static_cast<UINT32>(text.size()),
		&m_services.SystemCollection(),
		family.c_str(),
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		&mapped,
		&font,
		&scale), "MapCharacters");

	if (!font || mapped == 0)
		return std::nullopt;
	const auto name = GetFamilyName(font);
	if (name.empty())
		return std::nullopt;

	FontChanger::Structs::LookupStruct lookup;
	lookup.FamilyName = xivres::util::unicode::convert<std::string>(name);
	lookup.Weight = font->GetWeight();
	lookup.Stretch = font->GetStretch();
	lookup.Style = font->GetStyle();
	return std::make_pair(std::move(lookup), scale > 0 ? scale : 1.f);
}
