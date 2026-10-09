#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;

	// Splits words that don't fit on a line where DirectWrite allows a line break, never inside a cluster.
	//
	// The game wraps text once, when it is set (FUN_1406581A0: the chat log, word-wrapped text nodes, HandleFormatText). It
	// cuts the text into words at separator bytes and puts each on the current line if it fits; a word that doesn't goes to
	// FUN_14065B090, which returns how many of its units (characters and macros) stay on the line. That takes as many as fit
	// (FUN_14065AE60), then backs up to a break: anywhere between characters from U+2100 on (with the game's own rules for
	// CJK punctuation), after listed punctuation, or else, at the start of a line only, right at the fit. Such a forced break
	// can fall inside a cluster (a letter and its accent, a Thai consonant and its vowel, an Indic conjunct), and Thai, written
	// without spaces, has no breaks at all.
	//
	// For a word with marks or such a script, the split is chosen here instead: the last DirectWrite line break opportunity
	// (UAX #14, with dictionary breaks for Thai and the like) among the units that fit; at the start of a line, failing that,
	// the last whole cluster that fits, or one cluster. Other words keep the game's rules. The game builds a per-word table on
	// a word's first split and indexes it on the later ones, so a word is decided once, at its first split, and keeps that
	// decision.
	class LineBreaker {
		// A unit of a word: whether it is a macro taking no room, and where its text ends in the UTF-16 text.
		struct Unit {
			bool IsMacro;
			int TextEnd;
		};

		FontReplacer& m_replacer;
		using FitUnitsFn = int(*)(uintptr_t wrapper, const uint8_t* measuredWord, int width, int maxUnits, int* bytes);
		FitUnitsFn m_fitUnits = nullptr;

		// Wrapper: the units of the current word already put on earlier lines (0 on its first split), and the unit limit.
		int m_consumedUnitsOffset = 0;
		int m_maxUnitsOffset = 0;
		int m_usedUnitsOffset = 0;

		std::wstring m_text;
		std::vector<Unit> m_units;
		std::vector<uint8_t> m_clusterEnd;
		std::vector<uint8_t> m_wrapAfter;

		// The decision for the word being split, made at its first split.
		bool m_splittingOwnWord = false;

		std::optional<Host::Hook<uint32_t, uintptr_t, int, int, const uint8_t*, const uint8_t*, uint8_t>> m_splitWordHook;

	public:
		explicit LineBreaker(FontReplacer& replacer);
		LineBreaker(const LineBreaker&) = delete;
		LineBreaker& operator=(const LineBreaker&) = delete;

	private:
		// Gets whether a word needs clusters or dictionary breaks: it has combining marks, conjoining jamo, joiners, or
		// characters of a script written without spaces between words.
		static bool NeedsClusters(const uint8_t* p);

		uint32_t SplitWordDetour(uintptr_t wrapper, int width, int unused, const uint8_t* word, const uint8_t* measuredWord, uint8_t lineStart);

		uint32_t Split(uintptr_t wrapper, int width, const uint8_t* word, const uint8_t* measuredWord, bool lineStart);

		// Reads a word into units as the game counts them: a character, or a macro. Characters go to the UTF-16 text, icon
		// macros as an object replacement character (they take room like one), other macros as nothing.
		void Decode(const uint8_t* p);
	};
}
