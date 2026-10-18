#pragma once

#include <dwrite_3.h>
#include <FontChanger.Presets/Structs.h>

_COM_SMARTPTR_TYPEDEF(IDWriteFontFallback, __uuidof(IDWriteFontFallback));

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	// Finds the system font for text no font element has: private-use characters in the game's icon font, then Windows' UI fonts per script in
	// order, then the system's fallback.
	class SystemFallbackFonts {
	public:
		// Matched against when a preset font has no installed font of its own.
		static constexpr auto DefaultBaseFamily = "Segoe UI";

	private:
		FontServices& m_services;
		IDWriteFontFallbackPtr m_fallback;

	public:
		explicit SystemFallbackFonts(FontServices& services);
		SystemFallbackFonts(const SystemFallbackFonts&) = delete;
		SystemFallbackFonts& operator=(const SystemFallbackFonts&) = delete;

		// The font the start of text is drawn with, matched against baseFamily (the preset font's), and the scale of its size; nullopt if none has it.
		[[nodiscard]] std::optional<std::pair<FontChanger::Structs::LookupStruct, float>> Find(std::wstring_view text, std::string_view baseFamily);
	};
}
