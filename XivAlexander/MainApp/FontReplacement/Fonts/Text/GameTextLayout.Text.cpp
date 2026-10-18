#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/GameTextLayout.h"

#include "MainApp/FontReplacement/Fonts/Faces/MaterializedFontElement.h"
#include "MainApp/FontReplacement/Utilities/GameUtf8.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	constexpr uint8_t NewLineMacro = 0x10;
	constexpr uint8_t ItalicMacro = 0x1A;
}

int FontReplacement::GameTextLayout::ReadRun(const uint8_t* p, ItalicMode italic, int& length) {
	auto count = 0;
	auto i = 0;
	auto inItalic = italic != ItalicMode::Upright;
	while (i < MaxRunBytes) {
		const auto b = p[i];
		if (b == 0 || b == 0x0A || b == 0x0D)
			break;

		if (b == GameUtf8::Stx) {
			const auto macro = p[i + 1];
			if (macro == NewLineMacro || (macro == ItalicMacro && italic == ItalicMode::Images))
				break;
			const auto total = GameUtf8::MacroLength(p + i);
			if (total == 0 || i + total > MaxRunBytes)
				break;

			// <italic>'s argument must be a plain integer (1 on, 0 off); anything else ends the run.
			if (macro == ItalicMacro) {
				int payload, n, on, onLength;
				if (!GameUtf8::TryReadInteger(p + i + 2, payload, n) || !GameUtf8::TryReadInteger(p + i + 2 + n, on, onLength) || onLength != payload)
					break;
				inItalic = on != 0;
			}

			i += total;
			continue;
		}

		m_byteItalic[i] = inItalic ? 1 : 0;

		// Stepped as the game does.
		const auto charLength = GameUtf8::CharacterLength(p + i);
		if (charLength == 0 || i + charLength > MaxRunBytes)
			break;

		// Control characters and broken sequences shape as a replacement character, which keeps them per character.
		char32_t r = U'\xFFFD';
		const auto valid = b >= 0x20 && GameUtf8::TryDecode(p + i, charLength, r);
		if (!valid)
			r = U'\xFFFD';
		EnsureCapacity(count + 2);
		m_textToByte[count] = valid ? i : -1;
		const auto units = static_cast<int>(xivres::util::unicode::encode(&m_text[count], r));
		if (units == 2)
			m_textToByte[count + 1] = -1;
		count += units;
		i += charLength;
	}

	length = i;
	return count;
}

void FontReplacement::GameTextLayout::Shape(SizedFont& sized, const uint8_t* p, int count, std::vector<GameGlyph*>& glyphs, ItalicMode italic) {
	m_absorbed.clear();
	m_shapingSized = &sized;
	const auto cleanup = xivres::util::on_dtor([this] { m_shapingSized = nullptr; });

	count = ComposeHangul(count);
	AssignSources(count);

	if (italic != ItalicMode::Images) {
		// A code unit with no byte of its own is in the italics of the one before it.
		for (auto t = 0; t < count; t++) {
			const auto b = m_textToByte[t];
			m_textItalic[t] = b >= 0 ? m_byteItalic[b] : t > 0 ? m_textItalic[t - 1] : italic != ItalicMode::Upright;
		}

		Collect(count, false);
	} else if (!CollectSpreadItalics(count)) {
		std::fill_n(m_textItalic.begin(), count, static_cast<uint8_t>(0));
		Collect(count, true);
	}

	PlaceClusters(p, glyphs);

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
		const auto length = NextGrapheme(t, count);
		auto n = 0;
		for (auto u = t; u < t + length && n < MaxGraphemeRunes;) {
			const auto [rune, used] = DecodeAt(u, t + length).value_or(std::pair{U'\xFFFD', 1});
			runes[n++] = rune;
			u += used;
		}

		const auto source = m_shapingSized->Pick(std::span<const char32_t>(runes, n));

		// Codepoint replacements are written into the text in place, so only where the UTF-16 length stays the same.
		if (const auto element = source.Element; element && !element->DrawsGame()) {
			for (auto u = t; u < t + length;) {
				const auto decoded = DecodeAt(u, t + length);
				if (!decoded) {
					u++;
					continue;
				}

				const auto [rune, used] = *decoded;
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
