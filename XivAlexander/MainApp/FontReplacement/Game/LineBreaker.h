#pragma once

#include "MainApp/FontReplacement/Host.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontReplacer;

	class LineBreaker {
		// IsMacro is set only for macros that take no room; TextEnd indexes the UTF-16 text.
		struct Unit {
			bool IsMacro;
			int TextEnd;
		};

		FontReplacer& m_replacer;
		using FitUnitsFn = int(*)(uintptr_t wrapper, const uint8_t* measuredWord, int width, int maxUnits, int* bytes);
		FitUnitsFn m_fitUnits = nullptr;

		// ConsumedUnits counts the word's units put on earlier lines, so it is 0 at the word's first split.
		int m_consumedUnitsOffset = 0;
		int m_maxUnitsOffset = 0;
		int m_usedUnitsOffset = 0;

		std::wstring m_text;
		std::vector<Unit> m_units;
		std::vector<uint8_t> m_clusterEnd;
		std::vector<uint8_t> m_wrapAfter;

		// Decided at a word's first split, as its later splits can't switch to the game's.
		bool m_splittingOwnWord = false;

		std::optional<Host::Hook<uint32_t, uintptr_t, int, int, const uint8_t*, const uint8_t*, uint8_t>> m_splitWordHook;

	public:
		explicit LineBreaker(FontReplacer& replacer);
		LineBreaker(const LineBreaker&) = delete;
		LineBreaker& operator=(const LineBreaker&) = delete;

	private:
		// Only words with characters that join clusters or need dictionary breaks (class SA) are split here; the game splits the rest.
		static bool NeedsClusters(const uint8_t* p);

		// Index i of either span is the boundary after the first i UTF-16 code units; both must come zeroed and one longer than the text.
		static void GetBreaks(std::wstring_view text, std::span<uint8_t> clusterEnd, std::span<uint8_t> wrapAfter);

		uint32_t SplitWordDetour(uintptr_t wrapper, int width, int unused, const uint8_t* word, const uint8_t* measuredWord, uint8_t lineStart);

		uint32_t Split(uintptr_t wrapper, int width, const uint8_t* word, const uint8_t* measuredWord, bool lineStart);

		// Units are counted as the game does (a character or a macro); icon macros take room, so they become U+FFFC in the text.
		void Decode(const uint8_t* p);
	};
}
