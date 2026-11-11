#pragma once

#include "MainApp/FontReplacement/Fonts/LoadedFontSet.h"
#include "MainApp/FontReplacement/Fonts/SizedFont.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FamilyAtlases;
	class GlyphStore;

	// The sized fonts made so far, and the glyphs they cache.
	class SizedFontRegistry {
		// Glyphs cached by every sized font together.
		static constexpr size_t GlyphCapacity = 16384;

		std::unique_ptr<LoadedFontSet> m_fonts;
		FamilyAtlases& m_atlases;
		GlyphStore& m_store;
		const FrameState& m_state;
		std::map<std::tuple<LoadedFont*, int, std::string, bool>, std::shared_ptr<SizedFont>> m_sizedFonts;
		size_t m_sweepAt = GlyphCapacity;

		// Get's results by its arguments, as finding the family's fonts for a request is slow and wrapping asks per character. Which of the
		// family's fonts were ready is in them.
		std::map<std::tuple<GameFont*, int, bool>, std::shared_ptr<SizedFont>> m_byRequest;

	public:
		SizedFontRegistry(std::unique_ptr<LoadedFontSet> fonts, FamilyAtlases& atlases, GlyphStore& store, const FrameState& state);
		SizedFontRegistry(const SizedFontRegistry&) = delete;
		SizedFontRegistry& operator=(const SizedFontRegistry&) = delete;

		[[nodiscard]] LoadedFontSet& Fonts() const { return *m_fonts; }

		[[nodiscard]] auto All() const { return m_sizedFonts | std::views::values | std::views::transform([](const auto& s) { return s.get(); }); }

		// At the size of one of the family's fonts, that font's glyphs with its game font's metrics. At another, each glyph from the largest
		// font that draws it itself, with the metrics of the font that draws the most glyphs itself.
		const std::shared_ptr<SizedFont>& Get(GameFont* original, int halfPx, bool bold);

		// The glyphs used least recently of all sized fonts go, if they hold more than their shared capacity.
		void Sweep();

		void ReleaseAllGlyphs();

		// Lets go of their glyphs first.
		void Clear();

		// After Clear. The old fonts are freed.
		void SwapFonts(std::unique_ptr<LoadedFontSet> fonts);

	private:
		[[nodiscard]] size_t CountGlyphs() const;
	};
}
