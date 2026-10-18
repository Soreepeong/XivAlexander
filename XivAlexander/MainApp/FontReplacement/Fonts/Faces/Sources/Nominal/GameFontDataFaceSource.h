#pragma once

#include <FontChanger.Presets/Structs.h>

#include "MainApp/FontReplacement/Fonts/Faces/Sources/NominalFaceSource.h"

namespace xivres::fontdata {
	class stream;
}

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class GameFontDataFaceSource final : public NominalFaceSource {
		// Shared by every source and released with the last one.
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
		static std::unique_ptr<GameFontDataFaceSource> Create(const FontChanger::Structs::FontElement& def, const std::string& fontName);

		// Without textures, as FontsFromSystemFont only fits fonts to its metrics.
		static std::shared_ptr<FontChanger::FixedSizeFont::fixed_size_font> OpenMetrics(const std::string& fontName);

		const std::set<char32_t>& Codepoints() override { return m_codepoints; }
		uint32_t GetGlyphId(char32_t codepoint) override;
		float GetAdvance(float px, uint32_t glyphId) override;
		LineMetrics GetLineMetrics(float px) override;

	protected:
		std::optional<RasterGlyph> RasterizeGlyph(float px, uint32_t glyphId) override;

	private:
		static std::shared_ptr<Shared> GetShared();

		// 1 within the half-pixel rounding of copy sizes, so that those draw the font's own glyphs.
		[[nodiscard]] float GetScale(float px) const;
	};
}
