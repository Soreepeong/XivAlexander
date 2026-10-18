#pragma once

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;
	class SystemFallbackFonts;
	struct GameFont;

	// A font of a preset made usable (xivres merged_fixed_size_font): its elements and their sources, which element draws each grapheme, and its
	// line metrics at a drawn size. Sizes are relative to its first element, which gives the line metrics.
	// A codepoint goes to the elements in order: an element has it if it is in its ranges and its source has the glyph it draws it with; AddNew
	// takes it if no element has yet, AddAll always, Replace only from an earlier element. Characters no element has are the game's, or with system
	// fallback a system font's: an element is made for each font the system's fallback picks, at the primary element's size.
	class MaterializedFont {
		FontServices& m_services;
		std::shared_ptr<const FontChanger::Structs::Font> m_def;
		SystemFallbackFonts& m_fallbackFonts;
		bool m_systemFallback;
		float m_referenceSize;

		std::vector<std::unique_ptr<MaterializedFontElement>> m_elements;
		std::unique_ptr<MaterializedFontElement> m_gameElement;

		// The first element drawn from a font: its family is what system fonts are matched against, and its measuring mode is the font's.
		MaterializedFontElement* m_primary = nullptr;
		DWRITE_MEASURING_MODE m_measureMode = DWRITE_MEASURING_MODE_NATURAL;

		std::map<std::tuple<std::string, int, int, int, float>, std::unique_ptr<MaterializedFontElement>> m_fallbackElements;
		std::map<std::pair<GameFont*, char32_t>, MaterializedFontElement*> m_elementCache;
		std::optional<size_t> m_ownCodepointCount;

	public:
		// With systemFallback, characters no element has are drawn with the system fonts fallbackFonts finds.
		MaterializedFont(FontServices& services, std::shared_ptr<const FontChanger::Structs::Font> def, bool systemFallback, SystemFallbackFonts& fallbackFonts);
		MaterializedFont(const MaterializedFont&) = delete;
		MaterializedFont& operator=(const MaterializedFont&) = delete;
		~MaterializedFont();

		// For game fonts no preset gives a font: the game's glyphs, and with systemFallback, system fonts for what they lack.
		static std::unique_ptr<MaterializedFont> CreateGame(FontServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts);

		// Codepoints the font's elements draw themselves (not with the game's glyphs); kept once counted.
		[[nodiscard]] size_t OwnCodepointCount();

		// The element that draws a grapheme (one codepoint for the per-character path): the first that has all of it, else the one that has its first
		// codepoint, else a system font's; nullptr if none does. One that DrawsGame means the game's glyph is used.
		[[nodiscard]] MaterializedFontElement* TryGetElement(std::span<const char32_t> grapheme, GameFont* game);

		// The first element's (or the game font's), in whole pixels.
		[[nodiscard]] LineMetrics GetLineMetrics(float px, GameFont* game);

		// Where the font's line is at a drawn size, which its elements are aligned to.
		[[nodiscard]] FontAlignment GetAlignment(float px, GameFont* game);

		// In its cell (MaterializedFontElement::Wrap); nullopt if the game's glyph is to be used.
		[[nodiscard]] std::optional<RasterGlyph> TryRasterize(char32_t codepoint, float px, GameFont* game);

	private:
		// As FontChanger's FontElement::GetBaseFont makes them: by renderer, with gamma and glyph merging around it; nullptr for the game font being
		// replaced. A font that isn't installed has no glyphs.
		static std::unique_ptr<IFaceSource> CreateSource(const FontChanger::Structs::FontElement& def, FontServices& services, const std::string& fontName);

		// Its source's, or the game font's for the game's glyphs.
		[[nodiscard]] static LineMetrics GetElementMetrics(const MaterializedFontElement& element, float px, GameFont* game);

		// Every codepoint: with its source, or in the game font for the game's glyphs.
		[[nodiscard]] static bool Has(const MaterializedFontElement& element, std::span<const char32_t> grapheme, GameFont* game);

		// If it draws with fonts.
		void SetMeasureMode(IFaceSource* source) const;

		// The one assigned it, else a system font's or the game's.
		MaterializedFontElement* FindElement(std::span<const char32_t> grapheme, GameFont* game);

		// In order by their merge modes; nullptr if none.
		MaterializedFontElement* FindAssigned(std::span<const char32_t> grapheme, GameFont* game);

		// Of the system font the system's fallback draws a grapheme with, made on first use at the primary element's size (scaled as the fallback
		// says); nullptr if no font has it.
		MaterializedFontElement* GetFallbackElement(std::span<const char32_t> grapheme);
	};
}
