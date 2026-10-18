#pragma once

#include <dwrite_3.h>

#include "MainApp/FontReplacement/Fonts/Records/TextRun.h"

namespace XivAlexander::Apps::MainApp::FontReplacement {
	class FontServices;

	class DirectWriteTextAnalyzer {
		FontServices& m_services;
		std::vector<DWRITE_SCRIPT_ANALYSIS> m_scripts = std::vector<DWRITE_SCRIPT_ANALYSIS>(256);

	public:
		explicit DirectWriteTextAnalyzer(FontServices& services)
			: m_services(services) {}

		DirectWriteTextAnalyzer(const DirectWriteTextAnalyzer&) = delete;
		DirectWriteTextAnalyzer& operator=(const DirectWriteTextAnalyzer&) = delete;

		// The runs cover all of the text, in order.
		void Itemize(std::span<const wchar_t> text, std::vector<TextRun>& runs);
	};
}
