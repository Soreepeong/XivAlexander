#pragma once

#include "MainApp/FontReplacement/FontStructs.h"
#include "MainApp/FontReplacement/Fonts/Hooks/ReplacementGlyphLookup.h"
#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Has font sets pick copies, and answers glyph lookups in them. Anything that fails turns replacing off.
	class TextLayoutHooks {
		ReplacementFontTable& m_copies;
		GlyphStore& m_store;
		FrameState& m_state;
		LayoutContext& m_context;

		uintptr_t m_rendererVtbl = 0;
		uintptr_t m_renderCountVtbl = 0;
		void(*m_freeFontCache)(uintptr_t) = nullptr;

		// AtkTextNode offsets of its font manager cache entry (0 for none) and cache flags byte, whose UseFontCacheFlag asks for a cache.
		static constexpr uint8_t UseFontCacheFlag = 0x40;
		int m_fontCacheSlotOffset = 0;
		int m_fontCacheFlagsOffset = 0;

		// Before the hooks, so that it outlives them.
		std::optional<ReplacementGlyphLookup> m_lookup;

		std::optional<Host::Hook<GameGlyph*, GameFontSet*, uint32_t, GameFont*>> m_getGlyphHook;
		std::optional<Host::Hook<GameFont*, GameFontSet*, uint8_t>> m_pickFontHook;
		std::optional<Host::Hook<void, uintptr_t>> m_buildFontCacheHook;
		std::optional<Host::Hook<uintptr_t, uintptr_t, const uint8_t**, uintptr_t>> m_layOutCharacterHook;
		std::optional<Host::Hook<GameFontSet*, uintptr_t, uint8_t>> m_measureSetUpHook;
		std::optional<Host::Hook<int, uintptr_t, uintptr_t>> m_textWrapLayoutHook;

	public:
		// Throws if this game version can't be worked with.
		TextLayoutHooks(
			ReplacementFontTable& copies, SizedFontRegistry& sized, ShapedRunCache& layout, GlyphFactory& factory, GlyphAtlasPlacer& placer, GlyphStore& store, FrameState& state,
			LayoutContext& context);
		TextLayoutHooks(const TextLayoutHooks&) = delete;
		TextLayoutHooks& operator=(const TextLayoutHooks&) = delete;

		// Such text nodes are measured at their unscaled size, as they are baked at a scale of their own.
		std::function<bool(uintptr_t node)> IsNamePlateText;

		// A font cache keeps the glyph pointers laid out at the node's unscaled size, which fit only that size's copy and must not outlive the
		// replacement, so while replacing, nodes get no cache and are laid out at each draw.
		void UpdateFontCaches();

	private:
		void UpdateFontCaches(uintptr_t uld, int depth);

		void BuildFontCacheDetour(uintptr_t node);

		// Its callers lay the text out with the font set it returns, and restore the set afterwards.
		GameFontSet* MeasureSetUpDetour(uintptr_t node, uint8_t considerScale);

		// The text may come from a script or an addon, so there is no node to tell the scale it is drawn at. As glyphs of different sizes don't
		// scale evenly, it is wrapped to fit at every UI scale.
		int TextWrapLayoutDetour(uintptr_t wrapper, uintptr_t text);

		GameFont* PickFontDetour(GameFontSet* set, uint8_t useCache);

		uintptr_t LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state);

		GameGlyph* GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font);
	};
}
