#pragma once

#include <FontChanger.Presets/FontsFromSystemFont.h>

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FaceSourceCache;
	class FontServices;
	class SystemFallbackFonts;
	struct GameFont;

	// As xivres merged_fixed_size_font, elements take codepoints in order: AddNew those no earlier element has, AddAll any, Replace only those an
	// earlier element has.
	class MaterializedFont {
		FontServices& m_services;
		std::shared_ptr<const FontChanger::Structs::Font> m_def;
		SystemFallbackFonts& m_fallbackFonts;
		bool m_systemFallback;

		// Shared by the fonts made together.
		std::shared_ptr<FaceSourceCache> m_sources;

		// The font's sizes are relative to its first element's.
		float m_referenceSize;

		// Without the elements that couldn't be set up.
		std::vector<std::unique_ptr<MaterializedFontElement>> m_elements;
		std::unique_ptr<MaterializedFontElement> m_gameElement;

		// The first installed font's element: system fallback matches its family, and its measuring mode is the whole font's.
		MaterializedFontElement* m_primary = nullptr;
		DWRITE_MEASURING_MODE m_measureMode = DWRITE_MEASURING_MODE_NATURAL;

		std::map<std::tuple<std::string, int, int, int, float>, std::unique_ptr<MaterializedFontElement>> m_fallbackElements;

		// Fallback fonts fitted as chain items would be, on the first character each draws, and what fitting them opens; the
		// drawn probe refers to the opened fonts, so it is declared after them.
		std::map<std::tuple<std::string, int, int, int>, std::vector<std::unique_ptr<MaterializedFontElement>>> m_fittedFallback;
		std::map<std::tuple<std::string, DWRITE_FONT_WEIGHT, DWRITE_FONT_STRETCH, DWRITE_FONT_STYLE>, std::optional<FontChanger::FontsFromSystemFont::FontInfo>> m_probes;
		std::unique_ptr<FontChanger::FontsFromSystemFont::FontProbe> m_drawnProbe;
		std::optional<std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font>> m_gameProbe;

		std::map<std::pair<GameFont*, char32_t>, MaterializedFontElement*> m_elementCache;
		std::optional<size_t> m_ownCodepointCount;

	public:
		// An element that can't be set up is left out with a warning. Throws if none of them can.
		MaterializedFont(FontServices& services, std::shared_ptr<const FontChanger::Structs::Font> def, bool systemFallback, SystemFallbackFonts& fallbackFonts, std::shared_ptr<FaceSourceCache> sources);
		MaterializedFont(const MaterializedFont&) = delete;
		MaterializedFont& operator=(const MaterializedFont&) = delete;
		~MaterializedFont();

		static std::unique_ptr<MaterializedFont> CreateGame(FontServices& services, bool systemFallback, SystemFallbackFonts& fallbackFonts, std::shared_ptr<FaceSourceCache> sources);

		// Does the slow parts of the first lookups ahead, on the thread the font is made on.
		void Prepare();

		// Excludes codepoints drawn with the game's glyphs.
		[[nodiscard]] size_t OwnCodepointCount() {
			if (!m_ownCodepointCount)
				m_ownCodepointCount = CountOwnCodepoints();
			return *m_ownCodepointCount;
		}

		[[nodiscard]] MaterializedFontElement* TryGetElement(std::span<const char32_t> grapheme, GameFont* game);

		[[nodiscard]] LineMetrics GetLineMetrics(float px, GameFont* game);

		[[nodiscard]] FontAlignment GetAlignment(float px, GameFont* game);

		// nullopt means the game's glyph is used.
		[[nodiscard]] std::optional<RasterGlyph> TryRasterize(char32_t codepoint, float px, GameFont* game, bool bold);
	private:
		// As FontChanger's FontElement::GetBaseFont; nullptr for the replaced game font.
		std::shared_ptr<IFaceSource> CreateSource(const FontChanger::Structs::FontElement& def);

		// The size of the game font of the font's name; 0 if there is none.
		[[nodiscard]] float GetGameSize() const;

		void OpenFittingProbes();

		[[nodiscard]] static LineMetrics GetElementMetrics(const MaterializedFontElement& element, float px, GameFont* game);

		[[nodiscard]] static bool Has(const MaterializedFontElement& element, std::span<const char32_t> grapheme, GameFont* game);

		[[nodiscard]] size_t CountOwnCodepoints() const;

		MaterializedFontElement* FindElement(std::span<const char32_t> grapheme, GameFont* game);

		MaterializedFontElement* FindAssigned(std::span<const char32_t> grapheme, GameFont* game);

		MaterializedFontElement* GetFallbackElement(std::span<const char32_t> grapheme);

		// Fitted to the size the font's chain items were made at, the game font's.
		const std::vector<std::unique_ptr<MaterializedFontElement>>& GetFittedFallback(const FontChanger::Structs::LookupStruct& lookup);

		const FontChanger::FontsFromSystemFont::FontInfo* OpenProbe(const FontChanger::Structs::LookupStruct& lookup);
	};
}
