#include "pch.h"
#include "MainApp/FontReplacement/Fonts/FontReplacer.h"

#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"
#include "MainApp/FontReplacement/GameUi.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::FontReplacer::DisableAfterError() {
	m_enabled = false;
	ForgetPickedFonts();
}

void FontReplacement::FontReplacer::ForgetPickedFonts() {
	const auto manager = GameFontManager::Instance();
	if (!manager || !manager->FontSets())
		return;
	for (auto i = 0; i < GameFontManager::FontSetCount; i++) {
		const auto set = manager->FontSet(i);
		set->CurrentFont() = nullptr;
		set->CurrentFontSize() = 0;
	}
}

void FontReplacement::FontReplacer::BuildFontCacheDetour(uintptr_t node) {
	if (m_enabled)
		m_freeFontCache(node);
	else
		m_buildFontCacheHook->Original(node);
}

void FontReplacement::FontReplacer::UpdateFontCaches() {
	for (const auto unit : GameUi::GetLoadedUnits())
		UpdateFontCaches(GameUi::GetUnitUld(unit), 0);
}

void FontReplacement::FontReplacer::UpdateFontCaches(uintptr_t uld, int depth) {
	if (depth > 16)
		return;
	const auto count = GameUi::GetNodeCount(uld);
	for (auto i = 0; i < count; i++) {
		const auto node = GameUi::GetNode(uld, i);
		if (!node)
			continue;
		const auto type = GameUi::GetNodeType(node);
		if (type == GameUi::TextNodeType) {
			if (m_enabled) {
				if (*reinterpret_cast<const uint16_t*>(node + m_fontCacheSlotOffset))
					m_freeFontCache(node);
			} else if (*reinterpret_cast<const uint8_t*>(node + m_fontCacheFlagsOffset) & m_useFontCacheFlag) {
				m_buildFontCacheHook->Original(node);
			}
		} else if (type >= GameUi::FirstComponentNodeType) {
			if (const auto componentUld = GameUi::GetComponentUld(node))
				UpdateFontCaches(componentUld, depth + 1);
		}
	}
}

int FontReplacement::FontReplacer::BuildFontDetour(uintptr_t manager, uint16_t index) {
	const auto result = m_buildFontHook->Original(manager, index);
	try {
		const auto fonts = reinterpret_cast<GameFontManager*>(manager);
		if (index < fonts->FontCount())
			OnFontBuilt(fonts->Font(index));
	} catch (const std::exception& e) {
		Host::Error("Refreshing the copies of a rebuilt font failed; disabling: {}", e.what());
		DisableAfterError();
	}
	return result;
}

void FontReplacement::FontReplacer::OnFontBuilt(GameFont* font) {
	auto any = ForgetGameGlyphsOf(font);
	any |= ResyncCopies(font);
	if (any && TextInvalidated)
		TextInvalidated();
}

bool FontReplacement::FontReplacer::ForgetGameGlyphsOf(GameFont* font) {
	auto any = false;
	for (const auto& sized : m_sizedFonts | std::views::values) {
		if (std::ranges::find(sized->Sources, font) == sized->Sources.end()
			&& std::ranges::none_of(sized->ConvertedGlyphs | std::views::keys, [font](const auto& k) { return k.first == font; }))
			continue;
		sized->GameGlyphs.clear();
		sized->ConvertedGlyphs.clear();
		sized->Picks.clear();
		any = true;
	}
	return any;
}

uintptr_t FontReplacement::FontReplacer::UnloadFontsDetour(uintptr_t manager) {
	try {
		const auto& fonts = *reinterpret_cast<const GameFontManager*>(manager);
		std::set<uintptr_t> textures;
		for (auto i = 0, count = fonts.Fonts() ? static_cast<int>(fonts.FontCount()) : 0; i < count; i++) {
			const auto font = fonts.Font(i);
			if (!font->IsReady())
				continue;
			for (auto t = 0, n = (std::min)(static_cast<int>(font->TextureCount()), GameFont::MaxTextures); t < n; t++) {
				if (const auto texture = font->GetTexture(t))
					textures.insert(texture);
			}
			ForgetGameGlyphsOf(font);
		}

		DropGameReads(textures);
		ForgetPickedFonts();
		if (TextInvalidated)
			TextInvalidated();
	} catch (const std::exception& e) {
		Host::Error("Letting go of the fonts being unloaded failed; disabling: {}", e.what());
		m_gameReads.clear();
		DisableAfterError();
	}
	return m_unloadFontsHook->Original(manager);
}

void FontReplacement::FontReplacer::DropGameReads(const std::set<uintptr_t>& textures) {
	std::erase_if(m_gameReads, [&](GameGlyph* glyph) {
		auto& cell = CellOf(glyph);
		if (!cell.Source || !textures.contains(cell.Source->Texture))
			return false;
		ReleasePixels(cell);
		return true;
	});
}

FontReplacement::GameFont* FontReplacement::FontReplacer::PickFontDetour(GameFontSet* set, uint8_t useCache) {
	auto font = m_pickFontHook->Original(set, useCache);
	if (!font)
		return font;

	// The cached pick may be an earlier copy: re-decide from its game font, as the picker's cache is keyed by a size copies aren't chosen by.
	if (const auto it = m_copies.find(font); it != m_copies.end())
		font = it->second.Original;

	if (!m_enabled)
		return font;

	// Leave fonts not built yet (the glyph map may be gone).
	if (!font->IsReady())
		return font;

	try {
		ApplyPendingPages();

		// On-screen size even where the game picked another (fixed font resolution node's unscaled size, or its own pick scale), so nothing is scaled.
		const auto size = set->ScaledSizeY();
		const auto halfPx = std::clamp(static_cast<int>(StdRound(size * 2)), MinHalfPx, MaxHalfPx);
		const auto copy = GetOrCreateCopy(font, halfPx);

		// Rasterized at half-pixel sizes, but claim the exact size: any renderer scale other than exactly 1 resamples bilinearly (soft text), while
		// a quarter pixel size error doesn't show.
		if (halfPx < MaxHalfPx && halfPx > MinHalfPx)
			copy->Size() = size;
		set->CurrentFont() = copy;
		return copy;
	} catch (const std::exception& e) {
		Host::Error("Replacing a font failed; disabling: {}", e.what());
		DisableAfterError();
		return font;
	}
}

uintptr_t FontReplacement::FontReplacer::LayOutCharacterDetour(uintptr_t analyzer, const uint8_t** text, uintptr_t state) {
	const auto outer = s_current;
	const auto vtbl = *reinterpret_cast<const uintptr_t*>(analyzer);
	s_current = {
		.Character = *text,
		.MeasuringOnly = vtbl != m_rendererVtbl && vtbl != m_renderCountVtbl,
		.State = state,
	};
	const auto restore = xivres::util::on_dtor([&] {
		// The italic macro reads the bit when italics end (the glyph after moves by the font's italic correction).
		if (s_current.ItalicCleared)
			*reinterpret_cast<uint32_t*>(state + m_stateFlagsOffset) |= m_stateItalicFlag;
		s_current = outer;
	});

	// For an invalid lead byte the game takes its missing-glyph substitutes straight from the glyph map, which in a copy would draw a game glyph
	// from an atlas page; laid out as U+3013 instead, GetGlyph answers (for a game font, with what the game would take).
	static constexpr uint8_t Geta[]{0xE3, 0x80, 0x93, 0};
	if (const auto p = *text; m_enabled && ((*p >= 0x80 && *p < 0xC0) || *p >= 0xF8)) {
		s_current.Character = nullptr;
		*text = Geta;
		const auto res = m_layOutCharacterHook->Original(analyzer, text, state);
		*text = p + 1;
		return res;
	}
	return m_layOutCharacterHook->Original(analyzer, text, state);
}

FontReplacement::ItalicMode FontReplacement::FontReplacer::GetItalicMode(GameFontSet* set) const {
	if (m_stateFlagsOffset < 0 || !s_current.State || !(*reinterpret_cast<const uint32_t*>(s_current.State + m_stateFlagsOffset) & m_stateItalicFlag))
		return ItalicMode::Upright;
	return set && m_setFlagsOffset >= 0 && (*reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(set) + m_setFlagsOffset) & m_setItalicFlag)
		? ItalicMode::Images
		: ItalicMode::Real;
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetGlyphDetour(GameFontSet* set, uint32_t utf8Value, GameFont* font) {
	const auto it = m_copies.find(font);
	if (it == m_copies.end())
		return m_getGlyphHook->Original(set, utf8Value, font);
	const auto glyph = GetCopyGlyph(set, utf8Value, it->second);
	return glyph ? glyph : GetMissingGlyph(set, utf8Value, it->second);
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, const CopyInfo& info) {
	auto& sized = *info.Sized;

	// Laid out by LayOutCharacter at a known text position: its shaped cluster's glyph, unless this is a lookup of the game's fallback characters.
	if (const auto p = s_current.Character; p && GameUtf8::Pack(p) == utf8Value) {
		try {
			const auto italic = GetItalicMode(set);
			if (const auto shaped = m_layout->TryGetGlyph(sized, p, italic)) {
				// Real italics draw unsheared: clear the italic bit while laid out (quads are emitted after this lookup); LayOutCharacterDetour restores it.
				if (italic != ItalicMode::Upright && shaped != &m_emptyGlyph->Glyph && CellOf(shaped).RealItalic) {
					*reinterpret_cast<uint32_t*>(s_current.State + m_stateFlagsOffset) &= ~m_stateItalicFlag;
					s_current.ItalicCleared = true;
				}

				return WithPixels(shaped);
			}
		} catch (const std::exception& e) {
			Host::Error("Shaping failed; the character is laid out by itself: {}", e.what());
		}
	}

	auto it = sized.Glyphs.find(utf8Value);
	if (it == sized.Glyphs.end()) {
		GameGlyph* glyph;
		try {
			glyph = CreateGlyph(sized, utf8Value);
		} catch (const std::exception& e) {
			Host::Error("Rasterizing 0x{:X} at {} px failed: {}", utf8Value, sized.Px, e.what());
			glyph = nullptr;
		}
		it = sized.Glyphs.emplace(utf8Value, glyph).first;
	}

	if (it->second)
		return WithPixels(it->second);

	try {
		if (const auto own = GetGameGlyph(sized, utf8Value))
			return WithPixels(own);
	} catch (const std::exception& e) {
		Host::Error("Copying the game's glyph 0x{:X} failed: {}", utf8Value, e.what());
	}

	// The game's glyph, looked up in the game font, whose glyph map the copy borrows: its texture index is of the game font's textures, so it is
	// copied into the atlas too.
	const auto game = m_getGlyphHook->Original(set, utf8Value, info.Original);
	if (!game)
		return nullptr;
	try {
		return WithPixels(ConvertGameGlyph(sized, info.Original, game, utf8Value));
	} catch (const std::exception& e) {
		Host::Error("Copying the game's glyph 0x{:X} failed: {}", utf8Value, e.what());
		return &m_emptyGlyph->Glyph;
	}
}

FontReplacement::GameGlyph* FontReplacement::FontReplacer::GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, const CopyInfo& info) {
	for (const auto substitute : MissingGlyphSubstitutes) {
		if (utf8Value == substitute)
			continue;
		if (const auto glyph = GetCopyGlyph(set, substitute, info))
			return glyph;
	}
	return &m_emptyGlyph->Glyph;
}
