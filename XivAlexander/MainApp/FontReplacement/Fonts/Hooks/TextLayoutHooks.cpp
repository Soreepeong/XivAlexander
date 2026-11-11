#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontHooks.h"

#include "MainApp/FontReplacement/Fonts/CopyFonts.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontSetFlags.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutCall.h"
#include "MainApp/FontReplacement/GameCode.h"
#include "MainApp/FontReplacement/GameUi.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::FontHooks::FontHooks(
	CopyFonts& copies, SizedFontRegistry& sized, GameTextLayout& layout, GlyphFactory& factory, CellPlacer& placer, GlyphStore& store, FrameState& state,
	const FontSetFlags& flags)
	: m_copies(copies)
	, m_store(store)
	, m_state(state)
	, m_flags(flags) {
	const auto set = ResolveGameCode(Game::Resolved::Fonts::FontSet);
	const auto font = ResolveGameCode(Game::Resolved::Fonts::Font);
	const auto functions = ResolveGameCode(Game::Resolved::Fonts::Replacer);
	const auto measure = ResolveGameCode(Game::Resolved::Fonts::Measure);
	m_rendererVtbl = reinterpret_cast<uintptr_t>(functions.RendererVtable);
	m_renderCountVtbl = reinterpret_cast<uintptr_t>(functions.RenderCountVtable);
	m_fontCacheSlotOffset = functions.FontCacheSlot;
	m_fontCacheFlagsOffset = functions.FontCacheFlags;
	m_freeFontCache = reinterpret_cast<void(*)(uintptr_t)>(functions.FreeFontCache);

	m_lookup.emplace(sized, layout, factory, placer, store, state, flags, [this](GameFontSet* set, uint32_t utf8Value, GameFont* font) {
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

void FontReplacement::FontHooks::UpdateFontCaches() {
	for (const auto unit : GameUi::GetLoadedUnits())
		UpdateFontCaches(GameUi::GetUnitUld(unit), 0);
}

void FontReplacement::FontHooks::UpdateFontCaches(uintptr_t uld, int depth) {
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

void FontReplacement::FontHooks::BuildFontCacheDetour(uintptr_t node) {
	if (m_state.IsOn())
		m_freeFontCache(node);
	else
		m_buildFontCacheHook->Original(node);
}

FontReplacement::GameFontSet* FontReplacement::FontHooks::MeasureSetUpDetour(uintptr_t node, uint8_t considerScale) {
	const auto set = m_measureSetUpHook->Original(node, considerScale);
	if (!set || !node)
		return set;

	m_flags.ApplyNodeBold(set, node);

	// Hinting rounds glyphs of different sizes differently, so text measured in the node's units (a speech balloon's) is laid out with the
	// glyphs it is drawn with, of its size on screen. Nameplate bakes are left alone, being drawn at a scale of their own.
	if (m_state.IsOn() && considerScale == 0 && m_flags.PickScaleOf(set) <= 0 && !(IsNamePlateText && IsNamePlateText(node))) {
		const auto scale = m_flags.NodeScaleOf(node);
		if (std::isfinite(scale) && scale > 0 && std::abs(scale - 1) > 0.001f)
			m_flags.PickScaleOf(set) = scale;
	}
	return set;
}

int FontReplacement::FontHooks::TextWrapLayoutDetour(uintptr_t wrapper, uintptr_t text) {
	const auto outer = LayoutCall::Wrapping;
	LayoutCall::Wrapping = m_state.IsOn();
	const auto restore = xivres::util::on_dtor([outer] { LayoutCall::Wrapping = outer; });
	return m_textWrapLayoutHook->Original(wrapper, text);
}

FontReplacement::GameFont* FontReplacement::FontHooks::PickFontDetour(GameFontSet* set, uint8_t useCache) {
	auto font = m_pickFontHook->Original(set, useCache);
	if (!font)
		return font;

	// The picker's cache is keyed by its own size, not the one copies are chosen by, so a copy picked before is decided again.
	font = m_copies.OriginalOf(font);

	// A font not built yet (its glyph map may be gone) is left alone.
	if (!m_state.IsOn() || !font->IsReady())
		return font;

	try {
		// Picked for the on-screen size, even where the game picked for another (a node's fixed font resolution, or a pick scale of its own),
		// so that the renderer doesn't scale the glyphs. Text laid out unscaled takes its pick scale's.
		auto size = set->ScaledSizeY();
		if (set->NodeScaleX() == 1 && set->NodeScaleY() == 1) {
			if (const auto pickScale = m_flags.PickScaleOf(set); pickScale > 0 && std::isfinite(pickScale))
				size = m_flags.UnscaledSizeYOf(set) * pickScale;
			else if (LayoutCall::Wrapping)
				size *= GlyphLookup::WrapScales[std::size(GlyphLookup::WrapScales) - 1];
		}

		// Wrapped text keeps what it is laid out with.
		int halfPx;
		bool clamped;
		const auto copy = m_copies.Pick(font, size, !LayoutCall::Wrapping, halfPx, clamped);

		// Any renderer scale but exactly 1 resamples bilinearly (soft text), while a quarter pixel of size error doesn't show.
		if (halfPx < CopyFonts::MaxHalfPx && halfPx > CopyFonts::MinHalfPx)
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

uintptr_t FontReplacement::FontHooks::LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state) {
	const auto outer = LayoutCall::Current;
	const auto vtbl = *reinterpret_cast<const uintptr_t*>(analyzer);
	LayoutCall::Current = {
		.Character = *text,
		.MeasuringOnly = vtbl != m_rendererVtbl && vtbl != m_renderCountVtbl,
		.State = state,
	};
	const auto restore = xivres::util::on_dtor([&] {
		// The italic macro reads the bit when italics end (the glyph after moves by the font's italic correction).
		if (LayoutCall::Current.ItalicCleared)
			m_flags.SetStateItalic(state, true);
		LayoutCall::Current = outer;
	});

	// For an invalid lead byte the game takes its substitutes straight from the glyph map, which in a copy would draw a game glyph from an
	// atlas page, so U+3013 is laid out through GetGlyph instead.
	static constexpr uint8_t Geta[]{0xE3, 0x80, 0x93, 0};
	if (const auto p = *text; m_state.IsOn() && ((*p >= 0x80 && *p < 0xC0) || *p >= 0xF8)) {
		LayoutCall::Current.Character = nullptr;
		*text = Geta;
		const auto res = m_layOutCharacterHook->Original(analyzer, text, state);
		*text = p + 1;
		return res;
	}
	return m_layOutCharacterHook->Original(analyzer, text, state);
}

FontReplacement::GameGlyph* FontReplacement::FontHooks::GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font) {
	const auto info = m_copies.TryGet(font);
	if (!info)
		return m_getGlyphHook->Original(set, utf8Value, font);

	const auto glyph = m_lookup->Get(set, utf8Value, *info);
	m_store.Hand(glyph, m_state.Frame());
	return glyph;
}
