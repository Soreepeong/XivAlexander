#pragma once

#include <dwrite_3.h>
#include <FontChanger.Presets/Structs.h>

_COM_SMARTPTR_TYPEDEF(IDWriteFontFallback, __uuidof(IDWriteFontFallback));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	class SystemFallbackFonts {
	public:
		// For fonts that have no element drawn from an installed font.
		static constexpr auto DefaultBaseFamily = "Segoe UI";

	private:
		FontServices& m_services;
		IDWriteFontFallbackPtr m_fallback;

	public:
		explicit SystemFallbackFonts(FontServices& services);
		SystemFallbackFonts(const SystemFallbackFonts&) = delete;
		SystemFallbackFonts& operator=(const SystemFallbackFonts&) = delete;

		// baseFamily is the family the match is kept close to; nullopt if no font has the text's start.
		[[nodiscard]] std::optional<std::pair<FontChanger::Structs::LookupStruct, float>> Find(std::wstring_view text, std::string_view baseFamily);
	};
}
