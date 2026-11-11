#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/TextLayoutHooks.h"

#include "MainApp/FontReplacement/Fonts/ReplacementFontTable.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutContext.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::TextLayoutHooks::TextLayoutHooks(
	ReplacementFontTable& copies, SizedFontRegistry& sized, ShapedRunCache& layout, GlyphFactory& factory, GlyphAtlasPlacer& placer, GlyphStore& store, FrameState& state,
	LayoutContext& context)
	: m_copies(copies)
	, m_store(store)
	, m_state(state)
	, m_context(context) {
	const auto set = ResolveGameCode(Game::Resolved::Fonts::FontSet);
	const auto font = ResolveGameCode(Game::Resolved::Fonts::Font);
	const auto functions = ResolveGameCode(Game::Resolved::Fonts::Replacer);
	const auto measure = ResolveGameCode(Game::Resolved::Fonts::Measure);
	m_rendererVtbl = reinterpret_cast<uintptr_t>(functions.RendererVtable);
	m_renderCountVtbl = reinterpret_cast<uintptr_t>(functions.RenderCountVtable);
	m_fontCacheSlotOffset = functions.FontCacheSlot;
	m_fontCacheFlagsOffset = functions.FontCacheFlags;
	m_freeFontCache = reinterpret_cast<void(*)(uintptr_t)>(functions.FreeFontCache);

	m_lookup.emplace(sized, layout, factory, placer, store, state, context, [this](GameFontSet* set, uint32_t utf8Value, GameFont* font) {
		return m_getGlyphHook->Original(set, utf8Value, font);
	});
	m_getGlyphHook.emplace("GetGlyph", reinterpret_cast<uintptr_t>(font.GetGlyph), [this](GameFontSet* set, uint32_t utf8Value, GameFont* font) { return GetGlyphDetour(set, utf8Value, font); });
	m_pickFontHook.emplace("PickFont", reinterpret_cast<uintptr_t>(set.PickFont), [this](GameFontSet* set, uint8_t useCache) { return PickFontDetour(set, useCache); });
	m_buildFontCacheHook.emplace("BuildFontCache", reinterpret_cast<uintptr_t>(functions.BuildFontCache), [this](uintptr_t node) { BuildFontCacheDetour(node); });
	m_layOutCharacterHook.emplace("LayOutCharacter", reinterpret_cast<uintptr_t>(font.LayOutCharacter), [this](uintptr_t analyzer, const uint8_t** text, uintptr_t state) {
		return LayOutCharacterDetour(analyzer, text, state);
	});
	m_measureSetUpHook.emplace("MeasureSetUp", reinterpret_cast<uintptr_t>(measure.SetUp), [this](uintptr_t node, uint8_t considerScale) { return MeasureSetUpDetour(node, considerScale); });
	m_textWrapLayoutHook.emplace("TextWrapLayout", reinterpret_cast<uintptr_t>(measure.TextWrapLayout), [this](uintptr_t wrapper, uintptr_t text) {
		return TextWrapLayoutDetour(wrapper, text);
	});
}

void FontReplacement::TextLayoutHooks::UpdateFontCaches() {
	for (const auto unit : GameUi::GetLoadedUnits())
		UpdateFontCaches(GameUi::GetUnitUld(unit), 0);
}

void FontReplacement::TextLayoutHooks::UpdateFontCaches(uintptr_t uld, int depth) {
	if (depth > 16)
		return;
	const auto count = GameUi::GetNodeCount(uld);
	for (auto i = 0; i < count; i++) {
		const auto node = GameUi::GetNode(uld, i);
		if (!node)
			continue;
		const auto type = GameUi::GetNodeType(node);
		if (type == GameUi::TextNodeType) {
			if (m_state.IsOn()) {
				if (At<uint16_t>(node + m_fontCacheSlotOffset))
					m_freeFontCache(node);
			} else if (At<uint8_t>(node + m_fontCacheFlagsOffset) & UseFontCacheFlag) {
				m_buildFontCacheHook->Original(node);
			}
		} else if (type >= GameUi::FirstComponentNodeType) {
			if (const auto componentUld = GameUi::GetComponentUld(node))
				UpdateFontCaches(componentUld, depth + 1);
		}
	}
}

void FontReplacement::TextLayoutHooks::BuildFontCacheDetour(uintptr_t node) {
	if (m_state.IsOn())
		m_freeFontCache(node);
	else
		m_buildFontCacheHook->Original(node);
}

FontReplacement::GameFontSet* FontReplacement::TextLayoutHooks::MeasureSetUpDetour(uintptr_t node, uint8_t considerScale) {
	const auto set = m_measureSetUpHook->Original(node, considerScale);
	if (!set || !node)
		return set;

	m_context.ApplyNodeBold(set, node);

	// Hinting rounds glyphs of different sizes differently, so text measured in the node's units (a speech balloon's) is laid out with the
	// glyphs it is drawn with, of its size on screen. Nameplate bakes are left alone, being drawn at a scale of their own.
	if (m_state.IsOn() && considerScale == 0 && !m_context.HasPickScale(set)) {
		// Whether it is a nameplate's is asked last, as that looks through the loaded addons.
		const auto scale = m_context.NodeScaleOf(node);
		if (std::isfinite(scale) && scale > 0 && std::abs(scale - 1) > 0.001f && !(IsNamePlateText && IsNamePlateText(node)))
			m_context.PickScaleOf(set) = scale;
	}
	return set;
}

int FontReplacement::TextLayoutHooks::TextWrapLayoutDetour(uintptr_t wrapper, uintptr_t text) {
	const auto outer = m_context.EnterWrap(m_state.IsOn());
	const ScopeExit restore{[this, outer] { m_context.LeaveWrap(outer); }};
	return m_textWrapLayoutHook->Original(wrapper, text);
}

FontReplacement::GameFont* FontReplacement::TextLayoutHooks::PickFontDetour(GameFontSet* set, uint8_t useCache) {
	auto font = m_pickFontHook->Original(set, useCache);
	if (!font)
		return font;

	// The picker's cache is keyed by its own size, not the one copies are chosen by, so a copy picked before is decided again.
	font = m_copies.OriginalOf(font);

	// A font not built yet (its glyph map may be gone) is left alone.
	if (!m_state.IsOn() || !font->IsReady())
		return font;

	try {
		const auto size = m_context.PickSizeOf(set);

		// Wrapped text keeps what it is laid out with.
		int halfPx;
		bool clamped;
		const auto copy = m_copies.Pick(font, size, !m_context.Wrapping(), halfPx, clamped);

		// Any renderer scale but exactly 1 resamples bilinearly (soft text), while a quarter pixel of size error doesn't show.
		if (halfPx < ReplacementFontTable::MaxHalfPx && halfPx > ReplacementFontTable::MinHalfPx)
			copy->Size() = clamped ? static_cast<float>(halfPx) / 2 : size;
		set->CurrentFont() = copy;
		return copy;
	} catch (const std::exception& e) {
		// Failing forgets every set's pick, this one's too, which the game is about to lay out with.
		const auto pickedSize = set->CurrentFontSize();
		m_state.Fail("Replacing a font failed", e);
		set->CurrentFont() = font;
		set->CurrentFontSize() = pickedSize;
		return font;
	}
}

uintptr_t FontReplacement::TextLayoutHooks::LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state) {
	// For an invalid lead byte the game takes its substitutes straight from the glyph map, which in a copy would draw a game glyph from an
	// atlas page, so U+3013 is laid out through GetGlyph instead, as a character looked up by itself.
	static constexpr uint8_t Geta[]{0xE3, 0x80, 0x93, 0};
	const auto p = *text;
	const auto invalid = m_state.IsOn() && ((*p >= 0x80 && *p < 0xC0) || *p >= 0xF8);

	const auto vtbl = *reinterpret_cast<const uintptr_t*>(analyzer);
	const auto outer = m_context.EnterCharacter(invalid ? nullptr : p, vtbl != m_rendererVtbl && vtbl != m_renderCountVtbl, state);
	const ScopeExit restore{[&] { m_context.LeaveCharacter(outer); }};
	if (invalid) {
		*text = Geta;
		const auto res = m_layOutCharacterHook->Original(analyzer, text, state);
		*text = p + 1;
		return res;
	}
	return m_layOutCharacterHook->Original(analyzer, text, state);
}

FontReplacement::GameGlyph* FontReplacement::TextLayoutHooks::GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font) {
	const auto info = m_copies.TryGet(font);
	if (!info)
		return m_getGlyphHook->Original(set, utf8Value, font);

	const auto glyph = m_lookup->Get(set, utf8Value, *info);
	m_store.Hand(glyph, m_state.Frame());
	return glyph;
}
