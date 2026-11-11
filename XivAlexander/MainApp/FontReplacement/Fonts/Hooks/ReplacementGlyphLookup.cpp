#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/GlyphLookup.h"

#include "MainApp/FontReplacement/Fonts/CopyFonts.h"
#include "MainApp/FontReplacement/Fonts/FrameState.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/CellPlacer.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphFactory.h"
#include "MainApp/FontReplacement/Fonts/Hooks/FontSetFlags.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutCall.h"
#include "MainApp/FontReplacement/Fonts/SizedFontRegistry.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

FontReplacement::GlyphLookup::GlyphLookup(
	SizedFontRegistry& sized, GameTextLayout& layout, GlyphFactory& factory, CellPlacer& placer, GlyphStore& store, const FrameState& state,
	const FontSetFlags& flags, GameGlyphFunction gameGlyph)
	: m_sized(sized)
	, m_layout(layout)
	, m_factory(factory)
	, m_placer(placer)
	, m_store(store)
	, m_state(state)
	, m_flags(flags)
	, m_gameGlyph(std::move(gameGlyph)) {
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::Get(GameFontSet* set, uint32_t utf8Value, CopyInfo& info) {
	const auto drawn = GetDrawnSized(info);
	const auto wrapping = LayoutCall::Wrapping && set->NodeScaleX() == 1 && set->NodeScaleY() == 1 && m_flags.PickScaleOf(set) <= 0;
	if (wrapping) {
		if (const auto glyph = GetWrapRunGlyph(set, utf8Value, info, drawn))
			return glyph;
	}

	auto glyph = GetCopyGlyph(set, utf8Value, info, *drawn, true);
	if (!glyph)
		glyph = GetMissingGlyph(set, utf8Value, info, *drawn);
	return wrapping ? GetWrapGlyph(set, utf8Value, info, drawn, glyph) : glyph;
}

FontReplacement::ItalicMode FontReplacement::GlyphLookup::GetItalicMode(GameFontSet* set) const {
	if (!m_flags.IsStateItalic(LayoutCall::Current.State))
		return ItalicMode::Upright;
	return m_flags.IsSetItalic(set) ? ItalicMode::Images : ItalicMode::Real;
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::WithPixels(GameGlyph* glyph) {
	return LayoutCall::Current.MeasuringOnly ? glyph : m_placer.WithPixels(glyph, m_state.Frame());
}

std::span<FontReplacement::SizedFont* const> FontReplacement::GlyphLookup::GetWrapSizes(GameFontSet* set, CopyInfo& info, SizedFont* drawn) {
	return info.GetWrapSizes(m_sized, WrapScales, set->ScaledSizeY(), m_flags.IsBold(LayoutCall::Current.State), drawn);
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::GetWrapRunGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont* drawn) {
	const auto p = LayoutCall::Current.Character;
	if (!p || GameUtf8::Pack(p) != utf8Value)
		return nullptr;
	try {
		return m_layout.TryGetWrapGlyph(GetWrapSizes(set, info, drawn), WrapScales, p, GetItalicMode(set));
	} catch (const std::exception& e) {
		Host::Error("Laying out a run at the UI scales failed; it is measured a character at a time: {}", e.what());
		return nullptr;
	}
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::GetWrapGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont* drawn, GameGlyph* glyph) {
	if (!glyph || glyph == m_store.EmptyGlyph())
		return glyph;

	// In the copy's pixels, which are those of the largest scale. Width + OffsetX is how far the game advances.
	constexpr auto Largest = WrapScales[std::size(WrapScales) - 1];
	const auto own = glyph->Width + glyph->OffsetX;
	auto widest = own;
	try {
		const auto sizes = GetWrapSizes(set, info, drawn);
		for (size_t i = 0; i + 1 < sizes.size(); i++) {
			const auto other = GetCopyGlyph(set, utf8Value, info, *sizes[i], false);
			if (other && other != m_store.EmptyGlyph())
				widest = (std::max)(widest, static_cast<int>(std::ceil(static_cast<float>(other->Width + other->OffsetX) * Largest / WrapScales[i])));
		}
	} catch (const std::exception& e) {
		Host::Error("Measuring 0x{:X} at the other UI scales failed: {}", utf8Value, e.what());
		return glyph;
	}

	// A spacer advances at most 127.
	return widest == own || widest > 127 ? glyph : m_layout.GetSpacer(*drawn, widest);
}

FontReplacement::GameGlyph* FontReplacement::GlyphLookup::GetCopyGlyph(GameFontSet* set, uint32_t utf8Value, CopyInfo& info, SizedFont& font, bool shape) {
	// Not for a lookup of the game's fallback characters.
	auto& call = LayoutCall::Current;
	if (const auto p = call.Character; shape && p && GameUtf8::Pack(p) == utf8Value) {
		try {
			const auto italic = GetItalicMode(set);
			if (const auto shaped = m_layout.TryGetGlyph(font, p, italic)) {
				// The quad emitter runs after this lookup, so clearing the italic bit until LayOutCharacterDetour ends draws a real italic
				// glyph unsheared.
				if (italic != ItalicMode::Upright && GlyphStore::IsOwn(shaped) && m_store.CellOf(shaped).RealItalic) {
					m_flags.SetStateItalic(call.State, false);
					call.ItalicCleared = true;
				}

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
	if (!m_flags.IsBold(LayoutCall::Current.State))
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
	for (const auto substitute : MissingGlyphSubstitutes) {
		if (utf8Value == substitute)
			continue;
		if (const auto glyph = GetCopyGlyph(set, substitute, info, drawn, true))
			return glyph;
	}
	return m_store.EmptyGlyph();
}
