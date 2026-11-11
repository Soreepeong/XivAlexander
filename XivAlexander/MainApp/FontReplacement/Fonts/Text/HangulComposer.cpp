#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/HangulComposer.h"

namespace HangulComposer = XivAlexander::Apps::MainApp::FontReplacement::HangulComposer;

namespace {
	constexpr int SyllableBase = 0xAC00, LeadBase = 0x1100, VowelBase = 0x1161, TrailBase = 0x11A7;
	constexpr int LeadCount = 19, VowelCount = 21, TrailCount = 28, SCount = LeadCount * VowelCount * TrailCount;
}

int HangulComposer::Compose(std::span<wchar_t> text, std::span<int> textToByte, int count, std::vector<std::pair<int, int>>& absorbed) {
	auto w = 0;
	for (auto i = 0; i < count;) {
		int c = text[i];
		auto n = 1;
		if (c - LeadBase >= 0 && c - LeadBase < LeadCount && i + 1 < count && text[i + 1] - VowelBase >= 0 && text[i + 1] - VowelBase < VowelCount) {
			c = SyllableBase + ((c - LeadBase) * VowelCount + (text[i + 1] - VowelBase)) * TrailCount;
			n = 2;
		}

		if ((n == 2 || (c - SyllableBase >= 0 && c - SyllableBase < SCount && (c - SyllableBase) % TrailCount == 0)) &&
			i + n < count && text[i + n] - TrailBase > 0 && text[i + n] - TrailBase < TrailCount) {
			c += text[i + n] - TrailBase;
			n++;
		}

		for (auto j = 1; j < n; j++)
			absorbed.emplace_back(textToByte[i + j], textToByte[i]);
		text[w] = static_cast<wchar_t>(c);
		textToByte[w] = textToByte[i];
		w++;
		i += n;
	}
	return w;
}
