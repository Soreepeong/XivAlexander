#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Hooks/UiScaleWrapMeasurer.h"

#include "MainApp/FontReplacement/Fonts/ReplacementFontTable.h"
#include "MainApp/FontReplacement/Fonts/Glyphs/GlyphStore.h"
#include "MainApp/FontReplacement/Fonts/Hooks/ReplacementGlyphLookup.h"
#include "MainApp/FontReplacement/Fonts/Hooks/LayoutContext.h"
#include "MainApp/FontReplacement/Fonts/SizedFontRegistry.h"
#include "MainApp/FontReplacement/Fonts/Text/ShapedRunCache.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

std::span<FontReplacement::SizedFont* const> FontReplacement::UiScaleWrapMeasurer::GetSizes(GameFontSet* set, ReplacementFont& info, SizedFont* drawn) {
	return info.GetWrapSizes(m_sized, Scales, set->ScaledSizeY(), m_context.IsBold(), drawn);
}

FontReplacement::GameGlyph* FontReplacement::UiScaleWrapMeasurer::GetRunGlyph(GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont* drawn) {
	const auto p = m_context.Character();
	if (!p || GameUtf8::Pack(p) != utf8Value)
		return nullptr;
	try {
		return m_layout.TryGetWrapGlyph(GetSizes(set, info, drawn), Scales, p, m_context.ItalicModeOf(set));
	} catch (const std::exception& e) {
		Host::Error("Laying out a run at the UI scales failed; it is measured a character at a time: {}", e.what());
		return nullptr;
	}
}

FontReplacement::GameGlyph* FontReplacement::UiScaleWrapMeasurer::Widen(ReplacementGlyphLookup& lookup, GameFontSet* set, uint32_t utf8Value, ReplacementFont& info, SizedFont* drawn, GameGlyph* glyph) {
	if (!glyph || glyph == m_store.EmptyGlyph())
		return glyph;

	// In the copy's pixels, which are those of the largest scale. Width + OffsetX is how far the game advances.
	const auto own = glyph->Width + glyph->OffsetX;
	auto widest = own;
	try {
		const auto sizes = GetSizes(set, info, drawn);
		for (size_t i = 0; i + 1 < sizes.size(); i++) {
			const auto other = lookup.GetCopyGlyph(set, utf8Value, info, *sizes[i], false);
			if (other && other != m_store.EmptyGlyph())
				widest = (std::max)(widest, static_cast<int>(std::ceil(static_cast<float>(other->Width + other->OffsetX) * LargestScale / Scales[i])));
		}
	} catch (const std::exception& e) {
		Host::Error("Measuring 0x{:X} at the other UI scales failed: {}", utf8Value, e.what());
		return glyph;
	}

	// A spacer advances at most 127.
	return widest == own || widest > 127 ? glyph : m_layout.GetSpacer(*drawn, widest);
}
