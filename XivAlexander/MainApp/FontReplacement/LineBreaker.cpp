#include "pch.h"
#include "MainApp/FontReplacement/LineBreaker.h"

#include <icu.h>

#include "MainApp/FontReplacement/FontReplacer.h"
#include "MainApp/FontReplacement/GameLayout.h"
#include "MainApp/FontReplacement/GameText.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// Macros that the game counts as characters (icons).
	constexpr uint8_t IconMacro = 0x12;
	constexpr uint8_t Icon2Macro = 0x1E;

	// Characters the game's word gathering puts for the non-breaking space and hyphen macros.
	constexpr uint8_t NonBreakingSpaceChar = 0x1D;
	constexpr uint8_t HyphenChar = 0x1E;
}

FontReplacement::LineBreaker::LineBreaker(FontReplacer& replacer)
	: m_replacer(replacer) {
	uintptr_t splitWord = 0, fitUnits = 0;
	GameLayout::Resolve("Line breaking", [&] {
		splitWord = GameLayout::Address("SplitWord");
		fitUnits = GameLayout::Address("SplitWord", "FitUnits");
		m_consumedUnitsOffset = GameLayout::Get("Wrapper.ConsumedUnits");
		m_maxUnitsOffset = GameLayout::Get("Wrapper.MaxUnits");
		m_usedUnitsOffset = GameLayout::Get("Wrapper.UsedUnits");
	});
	m_fitUnits = reinterpret_cast<FitUnitsFn>(fitUnits);
	m_splitWordHook.emplace("SplitWord", splitWord, [this](uintptr_t wrapper, int width, int unused, const uint8_t* word, const uint8_t* measuredWord, uint8_t lineStart) {
		return SplitWordDetour(wrapper, width, unused, word, measuredWord, lineStart);
	});
}

bool FontReplacement::LineBreaker::NeedsClusters(const uint8_t* p) {
	while (*p) {
		if (*p == GameText::MacroStart) {
			const auto length = GameText::MacroLength(p);
			if (!length)
				return false;
			p += length;
			continue;
		}

		const auto n = GameText::CharacterLength(p);
		if (!n)
			return false;
		if (const auto c = GameUtf8::Decode(p, n); c >= 0) {
			const auto type = u_charType(c);
			if (type == U_NON_SPACING_MARK || type == U_COMBINING_SPACING_MARK || type == U_ENCLOSING_MARK
				|| (c >= 0x0E00 && c <= 0x0FFF)  // Thai, Lao, Tibetan
				|| (c >= 0x1000 && c <= 0x109F)  // Myanmar
				|| (c >= 0x1100 && c <= 0x11FF)  // Hangul jamo
				|| (c >= 0x1780 && c <= 0x17FF)  // Khmer
				|| c == 0x200C || c == 0x200D  // joiners
				|| (c >= 0xA960 && c <= 0xA97F) || (c >= 0xD7B0 && c <= 0xD7FF)  // Hangul jamo extended
				|| (c >= 0x1F1E6 && c <= 0x1F1FF))  // regional indicators
				return true;
		}

		p += n;
	}
	return false;
}

uint32_t FontReplacement::LineBreaker::SplitWordDetour(uintptr_t wrapper, int width, int unused, const uint8_t* word, const uint8_t* measuredWord, uint8_t lineStart) {
	const auto first = *reinterpret_cast<const int*>(wrapper + m_consumedUnitsOffset) == 0;
	try {
		if (first)
			m_splittingOwnWord = m_replacer.Enabled() && NeedsClusters(word);
		if (m_splittingOwnWord)
			return Split(wrapper, width, word, measuredWord, lineStart != 0);
	} catch (const std::exception& e) {
		Host::Error("Splitting a word failed: {}", e.what());

		// The game's split can take over only at a word's first split, as later ones index the table it built then; past that, force a split at the fit.
		if (!first) {
			const auto fit = m_fitUnits(wrapper, measuredWord, width, *reinterpret_cast<const int*>(wrapper + m_maxUnitsOffset) - *reinterpret_cast<const int*>(wrapper + m_usedUnitsOffset), nullptr);
			return fit <= 0 ? (lineStart ? 1u : 0u) : static_cast<uint32_t>(fit);
		}

		m_splittingOwnWord = false;
	}

	return m_splitWordHook->Original(wrapper, width, unused, word, measuredWord, lineStart);
}

uint32_t FontReplacement::LineBreaker::Split(uintptr_t wrapper, int width, const uint8_t* word, const uint8_t* measuredWord, bool lineStart) {
	const auto fit = m_fitUnits(wrapper, measuredWord, width, *reinterpret_cast<const int*>(wrapper + m_maxUnitsOffset) - *reinterpret_cast<const int*>(wrapper + m_usedUnitsOffset), nullptr);
	if (fit < 0)
		return 0;

	Decode(word);
	const auto count = static_cast<int>(m_units.size());
	if (fit >= count)
		return static_cast<uint32_t>(count);

	m_clusterEnd.assign(m_text.size() + 1, 0);
	m_wrapAfter.assign(m_text.size() + 1, 0);
	m_replacer.Shaper().GetBreaks(m_text, m_clusterEnd, m_wrapAfter);

	// The last break opportunity among the units that fit, looked up at each unit's text end; a macro has none of its own.
	auto best = 0;
	for (auto k = 1; k <= fit; k++) {
		const auto end = m_units[k - 1].TextEnd;
		if (end > 0 && m_wrapAfter[end])
			best = k;
	}

	// At the start of a line something has to go on it: the whole clusters that fit, or the first cluster.
	if (best == 0 && lineStart) {
		for (auto k = 1; k <= fit; k++) {
			const auto end = m_units[k - 1].TextEnd;
			if (end > 0 && m_clusterEnd[end])
				best = k;
		}

		for (auto k = fit + 1; best == 0 && k <= count; k++) {
			const auto end = m_units[k - 1].TextEnd;
			if (end > 0 && m_clusterEnd[end])
				best = k;
		}

		if (best == 0)
			best = count;
	}

	// Macros (colours and the like) right after the break stay with the line they follow, as in the game's split.
	while (best > 0 && best < count && m_units[best].IsMacro)
		best++;

	return static_cast<uint32_t>(best);
}

void FontReplacement::LineBreaker::Decode(const uint8_t* p) {
	m_units.clear();
	m_text.clear();
	while (*p) {
		if (*p == GameText::MacroStart) {
			const auto length = GameText::MacroLength(p);
			if (!length)
				break;
			const auto icon = p[1] == IconMacro || p[1] == Icon2Macro;
			if (icon)
				m_text.push_back(L'\xFFFC');
			m_units.push_back({!icon, static_cast<int>(m_text.size())});
			p += length;
			continue;
		}

		const auto n = GameText::CharacterLength(p);
		if (!n)
			break;
		char32_t rune;
		if (*p == NonBreakingSpaceChar) {
			rune = 0xA0;
		} else if (*p == HyphenChar) {
			rune = U'-';
		} else {
			const auto c = GameUtf8::Decode(p, n);
			rune = c < 0 ? U'\xFFFD' : static_cast<char32_t>(c);
		}

		if (rune < 0x10000) {
			m_text.push_back(static_cast<wchar_t>(rune));
		} else {
			m_text.push_back(static_cast<wchar_t>(0xD800 + ((rune - 0x10000) >> 10)));
			m_text.push_back(static_cast<wchar_t>(0xDC00 + ((rune - 0x10000) & 0x3FF)));
		}
		m_units.push_back({false, static_cast<int>(m_text.size())});
		p += n;
	}
}
