#pragma once

// Composes conjoining jamo into precomposed syllables as NFC does, since DirectWrite has no Hangul shaping and would draw each jamo as a
// separate full-width letter. Old Hangul, having no precomposed syllables, is left as it is.
namespace XivAlexander::Apps::MainApp::FontReplacement::HangulComposer {
	// Composes the first count code units of text in place, and returns the count left. textToByte has each code unit's byte, which moves
	// with it. absorbed gets the bytes of the jamo composed into a syllable, with that syllable's byte.
	int Compose(std::span<wchar_t> text, std::span<int> textToByte, int count, std::vector<std::pair<int, int>>& absorbed);
}
