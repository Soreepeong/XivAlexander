#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The codepoint at u of text[..end], and its length in code units; U+FFFD and 1 for a lone surrogate.
	std::pair<char32_t, int> DecodeAt(const std::vector<wchar_t>& text, int u, int end) {
		const auto c = text[u];
		if (IS_HIGH_SURROGATE(c) && u + 1 < end && IS_LOW_SURROGATE(text[u + 1]))
			return {static_cast<char32_t>(0x10000 + ((c - 0xD800) << 10) + (text[u + 1] - 0xDC00)), 2};
		if (FontReplacement::IsSurrogate(c))
			return {U'\xFFFD', 1};
		return {static_cast<char32_t>(c), 1};
	}
}

void FontReplacement::GameTextLayout::Shape(SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic) {
	m_absorbed.clear();
	m_shapingBytes = p;
	m_shapingGlyphs = &glyphs;
	m_shapingSized = &sized;
	const auto cleanup = xivres::util::on_dtor([this] {
		m_shapingBytes = nullptr;
		m_shapingGlyphs = nullptr;
		m_shapingSized = nullptr;
	});

	count = ComposeHangul(count);
	AssignSources(count);

	// Each code unit is in italics as its character's byte is (a low surrogate or control character as the one before).
	for (auto t = 0; t < count; t++) {
		const auto b = m_textToByte[t];
		m_textItalic[t] = b >= 0 ? m_byteItalic[b] : t > 0 ? m_textItalic[t - 1] : italic != ItalicMode::Upright;
	}

	// A run in a node's italics is spaced to its upright width (or drawn per character at its upright places); the others are shaped whole, in
	// italics where their italic macros say.
	if (italic != ItalicMode::Images) {
		Collect(count, false);
	} else if (!CollectSpreadItalics(count)) {
		std::fill_n(m_textItalic.begin(), count, static_cast<uint8_t>(0));
		Collect(count, true);
	}

	PlaceClusters();

	// A jamo composed into its syllable is part of the syllable's cluster.
	for (const auto& [b, owner] : m_absorbed) {
		if (b >= 0 && owner >= 0)
			glyphs[b] = glyphs[owner] ? m_emptyGlyph : nullptr;
	}
}

int FontReplacement::GameTextLayout::ComposeHangul(int count) {
	constexpr int SyllableBase = 0xAC00, LeadBase = 0x1100, VowelBase = 0x1161, TrailBase = 0x11A7;
	constexpr int LeadCount = 19, VowelCount = 21, TrailCount = 28, SCount = LeadCount * VowelCount * TrailCount;

	auto w = 0;
	for (auto i = 0; i < count;) {
		int c = m_text[i];
		auto n = 1;
		if (c - LeadBase >= 0 && c - LeadBase < LeadCount && i + 1 < count && m_text[i + 1] - VowelBase >= 0 && m_text[i + 1] - VowelBase < VowelCount) {
			c = SyllableBase + ((c - LeadBase) * VowelCount + (m_text[i + 1] - VowelBase)) * TrailCount;
			n = 2;
		}

		if ((n == 2 || (c - SyllableBase >= 0 && c - SyllableBase < SCount && (c - SyllableBase) % TrailCount == 0)) &&
			i + n < count && m_text[i + n] - TrailBase > 0 && m_text[i + n] - TrailBase < TrailCount) {
			c += m_text[i + n] - TrailBase;
			n++;
		}

		for (auto j = 1; j < n; j++)
			m_absorbed.emplace_back(m_textToByte[i + j], m_textToByte[i]);
		m_text[w] = static_cast<wchar_t>(c);
		m_textToByte[w] = m_textToByte[i];
		w++;
		i += n;
	}
	return w;
}

void FontReplacement::GameTextLayout::AssignSources(int count) {
	char32_t runes[MaxGraphemeRunes];
	for (auto t = 0; t < count;) {
		const auto length = (std::max)(1, NextGrapheme(t, count));
		auto n = 0;
		for (auto u = t; u < t + length && n < MaxGraphemeRunes;) {
			const auto [rune, used] = DecodeAt(m_text, u, t + length);
			runes[n++] = rune;
			u += used;
		}

		const auto source = m_shapingSized->Pick(std::span<const char32_t>(runes, n));
		if (const auto element = source.Element; element && !element->DrawsGame()) {
			for (auto u = t; u < t + length;) {
				const auto [rune, used] = DecodeAt(m_text, u, t + length);
				if (rune == U'\xFFFD' && used == 1 && IsSurrogate(m_text[u])) {
					u++;
					continue;
				}

				const auto drawn = element->GetDrawnCodepoint(rune);
				if (drawn != rune && xivres::util::unicode::encode<wchar_t>(nullptr, drawn) == static_cast<size_t>(used))
					xivres::util::unicode::encode(&m_text[u], drawn);
				u += used;
			}
		}

		std::fill_n(m_textSource.begin() + t, length, std::optional(source));
		t += length;
	}
}
