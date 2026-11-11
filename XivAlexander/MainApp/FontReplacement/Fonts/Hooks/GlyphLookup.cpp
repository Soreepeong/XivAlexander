#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/GlyphLookup.h"

#include "MainApp/FontReplacement/Fonts/CopyFonts.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/CellPlacer.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphFactory.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutContext.h"
#include "MainApp/FontReplacement/Fonts/SizedFontRegistry.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::GlyphLookup::GlyphLookup(
	SizedFontRegistry& sized, GameTextLayout& layout, GlyphFactory& factory, CellPlacer& placer, GlyphStore& store, const FrameState& state,
	LayoutContext& context, GameGlyphFunction gameGlyph)
	: m_sized(sized)
	, m_layout(layout)
	, m_factory(factory)
	, m_placer(placer)
	, m_store(store)
	, m_state(state)
	, m_context(context)
	, m_gameGlyph(std::move(gameGlyph))
	, m_wrap(sized, layout, store, context) {
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::Get(GameFontSet* set, uint32_t utf8Value, CopyInfo& info) {
	const auto drawn = GetDrawnSized(info);
	const auto wrapping = m_context.IsWrapMeasure(set);
	if (wrapping) {
		if (const auto glyph = m_wrap.GetRunGlyph(set, utf8Value, info, drawn))
			return glyph;
	}

	auto glyph = GetCopyGlyph(set, utf8Value, info, *drawn, true);
	if (!glyph)
		glyph = GetMissingGlyph(set, utf8Value, info, *drawn);
	return wrapping ? m_wrap.Widen(*this, set, utf8Value, info, drawn, glyph) : glyph;
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::WithPixels(GameGlyph* glyph) {
	return m_context.MeasuringOnly() ? glyph : m_placer.WithPixels(glyph, m_state.Frame());
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& font, bool shape) {
	// Not for a lookup of the game's fallback characters.
	if (const auto p = m_context.Character(); shape && p && GameUtf8::Pack(p) == utf8Value) {
		try {
			const auto italic = m_context.ItalicModeOf(set);
			if (const auto shaped = m_layout.TryGetGlyph(font, p, italic)) {
				// The quad emitter runs after this lookup, so clearing the italic bit until LayOutCharacterDetour ends draws a real italic
				// glyph unsheared.
				if (italic != ItalicMode::Upright && GlyphStore::IsOwn(shaped) && m_store.CellOf(shaped).RealItalic)
					m_context.ClearItalic();

				return WithPixels(shaped);
			}
		} catch (const std::exception& e) {
			Host::Error("Shaping failed; the character is laid out by itself: {}", e.what());
		}
	}

	auto found = font.Glyphs.Find(utf8Value);
	if (!found)
		found = &font.Glyphs.Add(utf8Value, MakeGlyph(set, utf8Value, info, font));
	return *found ? WithPixels(*found) : nullptr;
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::MakeGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& font) {
	try {
		if (const auto own = m_factory.FromCodepoint(font, utf8Value))
			return own;
	} catch (const std::exception& e) {
		Host::Error("Rasterizing 0x{:X} at {} px failed: {}", utf8Value, font.Px, e.what());
	}

	try {
		if (const auto game = m_factory.GameGlyphOf(font, utf8Value))
			return game;
	} catch (const std::exception& e) {
		Host::Error("Copying the game's glyph 0x{:X} failed: {}", utf8Value, e.what());
	}

	// The game font's own glyph has a texture index of the game font's textures, so it is copied into the atlas too.
	const auto found = m_gameGlyph(set, utf8Value, info.Original);
	if (!found)
		return nullptr;
	try {
		return m_factory.Convert(font, info.Original, found, utf8Value);
	} catch (const std::exception& e) {
		Host::Error("Copying the game's glyph 0x{:X} failed: {}", utf8Value, e.what());
		return m_store.EmptyGlyph();
	}
}

FontReplacement::SizedFont* FontReplacement::GlyphLookup::GetDrawnSized(CopyInfo& info) {
	if (!m_context.IsBold())
		return info.Sized();
	try {
		return info.GetBold(m_sized);
	} catch (const std::exception& e) {
		Host::Error("Setting up the fonts of bold failed: {}", e.what());
		info.BoldFailed();
		return info.Sized();
	}
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::GetMissingGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& drawn) {
	for (const auto substitute : GameUtf8::MissingGlyphSubstitutes) {
		if (utf8Value == substitute)
			continue;
		if (const auto glyph = GetCopyGlyph(set, substitute, info, drawn, true))
			return glyph;
	}
	return m_store.EmptyGlyph();
}
