#include "pch.h"
#include "MainApp/FontReplacement/LineBreaker.h"

#include <icu.h>
#include <xivres/xivstring.h>

#include "MainApp/FontReplacement/FontReplacer.h"
#include "MainApp/FontReplacement/GameCode.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	std::span<const uint8_t> AsText(const uint8_t* p) {
		return {p, std::strlen(reinterpret_cast<const char*>(p))};
	}

	// Macros that the game counts as characters (icons).
	constexpr uint8_t IconMacro = 0x12;
	constexpr uint8_t Icon2Macro = 0x1E;

	// Characters the game's word gathering puts for the non-breaking space and hyphen macros.
	constexpr uint8_t NonBreakingSpaceChar = 0x1D;
	constexpr uint8_t HyphenChar = 0x1E;
}

FontReplacement::LineBreaker::LineBreaker(FontReplacer& replacer)
	: m_replacer(replacer) {
	const auto functions = ResolveGameCode(Game::Resolved::Fonts::LineBreaker);
	m_consumedUnitsOffset = functions.ConsumedUnits;
	m_maxUnitsOffset = functions.MaxUnits;
	m_usedUnitsOffset = functions.UsedUnits;
	m_fitUnits = reinterpret_cast<FitUnitsFn>(functions.FitUnits);
	m_splitWordHook.emplace("SplitWord", reinterpret_cast<uintptr_t>(functions.SplitWord), [this](uintptr_t wrapper, int width, int unused, const uint8_t* word, const uint8_t* measuredWord, uint8_t lineStart) {
		return SplitWordDetour(wrapper, width, unused, word, measuredWord, lineStart);
	});
}

bool FontReplacement::LineBreaker::NeedsClusters(const uint8_t* word) {
	const auto text = AsText(word);
	for (size_t i = 0; i < text.size();) {
		const auto rest = text.subspan(i);
		if (rest[0] == static_cast<uint8_t>(xivres::xivstring::StartOfText)) {
			const auto macro = xivres::xivstring::read_macro(rest);
			if (!macro)
				return false;
			i += macro->size;
			continue;
		}

		const auto n = xivres::xivstring::character_length(rest);
		if (!n)
			return false;
		if (const auto c = GameUtf8::Decode(rest.data(), static_cast<int>(n)); c >= 0) {
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

		i += n;
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

	// A macro has no break opportunity of its own.
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

void FontReplacement::LineBreaker::Decode(const uint8_t* word) {
	m_units.clear();
	m_text.clear();
	const auto text = AsText(word);
	for (size_t i = 0; i < text.size();) {
		const auto rest = text.subspan(i);
		if (rest[0] == static_cast<uint8_t>(xivres::xivstring::StartOfText)) {
			const auto macro = xivres::xivstring::read_macro(rest);
			if (!macro)
				break;
			const auto type = static_cast<uint8_t>(macro->type);
			const auto icon = type == IconMacro || type == Icon2Macro;
			if (icon)
				m_text.push_back(L'\xFFFC');
			m_units.push_back({!icon, static_cast<int>(m_text.size())});
			i += macro->size;
			continue;
		}

		const auto n = xivres::xivstring::character_length(rest);
		if (!n)
			break;
		char32_t rune;
		if (rest[0] == NonBreakingSpaceChar) {
			rune = 0xA0;
		} else if (rest[0] == HyphenChar) {
			rune = U'-';
		} else {
			const auto c = GameUtf8::Decode(rest.data(), static_cast<int>(n));
			rune = c < 0 ? U'\xFFFD' : static_cast<char32_t>(c);
		}

		if (rune < 0x10000) {
			m_text.push_back(static_cast<wchar_t>(rune));
		} else {
			m_text.push_back(static_cast<wchar_t>(0xD800 + ((rune - 0x10000) >> 10)));
			m_text.push_back(static_cast<wchar_t>(0xDC00 + ((rune - 0x10000) & 0x3FF)));
		}
		m_units.push_back({false, static_cast<int>(m_text.size())});
		i += n;
	}
}
