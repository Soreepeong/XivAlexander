#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"

namespace xivres::fontdata {
	class stream;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A game font's glyphs read from the game's files.
	class GameFontDataFaceSource final : public NominalFaceSource {
		// The installation and the textures read from it, kept while any source is.
		struct Shared;

		static std::mutex s_sharedMutex;
		static std::weak_ptr<Shared> s_shared;

		std::shared_ptr<Shared> m_shared;
		std::shared_ptr<const xivres::fontdata::stream> m_fdt;
		std::string m_textures;
		float m_size;
		std::set<char32_t> m_codepoints;

		GameFontDataFaceSource(std::shared_ptr<Shared> shared, std::shared_ptr<const xivres::fontdata::stream> fdt, std::string textures, float size);

	public:
		~GameFontDataFaceSource() override;

		// The font of the element's game font family closest to its size; nullptr if there is none, or it can't be read.
		static std::unique_ptr<GameFontDataFaceSource> Create(const FontChanger::Structs::FontElement& def, const std::string& fontName);

		// A game font by its name with its glyphs' metrics but no textures, for FontsFromSystemFont to fit fonts to; nullptr
		// if there is none, or it can't be read.
		static std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> OpenMetrics(const std::string& fontName);

		const std::set<char32_t>& Codepoints() override { return m_codepoints; }
		uint32_t GetGlyphId(char32_t codepoint) override;
		float GetAdvance(float px, uint32_t glyphId) override;
		LineMetrics GetLineMetrics(float px) override;

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override;

	private:
		static std::shared_ptr<Shared> GetShared();

		// From the game font's glyphs to a size of the element: 1 within the half-pixel rounding of copy sizes.
		[[nodiscard]] float GetScale(float px) const;
	};
}
