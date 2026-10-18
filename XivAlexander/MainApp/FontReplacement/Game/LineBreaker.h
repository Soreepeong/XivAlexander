#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;

	// Splits words that don't fit (SplitWord) where UAX #14 allows a line break, never inside a grapheme cluster, if they have characters that join
	// clusters or of scripts without spaces, which the game's split breaks; at line start, failing that, the last whole cluster that fits, or one.
	// A word is decided at its first split, as the game's later splits depend on it.
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
		// True for a character that can join a cluster with one next to it (marks, joiners, jamo, regional indicators), or of a script written
		// without spaces between words (line break class SA).
		static bool NeedsClusters(const uint8_t* p);

		// [i] is set if a cluster ends (UAX #29) / a line may also break (UAX #14, dictionary breaks for Thai) after i code units; both text.size()+1 long.
		static void GetBreaks(std::wstring_view text, std::span<uint8_t> clusterEnd, std::span<uint8_t> wrapAfter);

		uint32_t SplitWordDetour(uintptr_t wrapper, int width, int unused, const uint8_t* word, const uint8_t* measuredWord, uint8_t lineStart);

		uint32_t Split(uintptr_t wrapper, int width, const uint8_t* word, const uint8_t* measuredWord, bool lineStart);

		// Units as the game counts them (a character or a macro); icon macros become U+FFFC in m_text, as they take room, other macros nothing.
		void Decode(const uint8_t* p);
	};
}
