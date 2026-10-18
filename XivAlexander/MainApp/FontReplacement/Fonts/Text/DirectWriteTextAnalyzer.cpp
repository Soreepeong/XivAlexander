#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/DirectWriteTextAnalyzer.h"

#include "MainApp/FontReplacement/Fonts/FontServices.h"
#include "MainApp/FontReplacement/Fonts/Text/TextAnalysisSource.h"
#include "MainApp/FontReplacement/Utilities/Misc.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// The plugin's RuneRanges.RightToLeft: Hebrew through Arabic Extended-A, the Hebrew and Arabic presentation forms, and the RTL blocks of the SMP.
	constexpr std::pair<char32_t, char32_t> RightToLeftRanges[]{
		{0x0590, 0x08FF},
		{0xFB1D, 0xFDFF},
		{0xFE70, 0xFEFF},
		{0x10800, 0x10FFF},
		{0x1E800, 0x1EFFF},
	};

	bool IsRightToLeft(std::span<const wchar_t> text, size_t index) {
		char32_t c = text[index];
		if (IS_HIGH_SURROGATE(text[index]) && index + 1 < text.size() && IS_LOW_SURROGATE(text[index + 1]))
			c = 0x10000 + ((text[index] - 0xD800) << 10) + (text[index + 1] - 0xDC00);
		else if (FontReplacement::IsSurrogate(text[index]))
			return false;
		return std::ranges::any_of(RightToLeftRanges, [c](const auto& r) { return c >= r.first && c <= r.second; });
	}

	FontReplacement::ScriptToken ToToken(const DWRITE_SCRIPT_ANALYSIS& a) {
		return {a.script, static_cast<int>(a.shapes)};
	}

	// Lives where it is declared, as the source does.
	class ScriptSink final : public IDWriteTextAnalysisSink {
		std::span<DWRITE_SCRIPT_ANALYSIS> m_scripts;

	public:
		explicit ScriptSink(std::span<DWRITE_SCRIPT_ANALYSIS> scripts)
			: m_scripts(scripts) {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
			if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSink)) {
				*ppv = static_cast<IDWriteTextAnalysisSink*>(this);
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }

		HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32 position, UINT32 length, const DWRITE_SCRIPT_ANALYSIS* analysis) override {
			for (auto i = position; i < position + length && i < m_scripts.size(); i++)
				m_scripts[i] = *analysis;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT*) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32, UINT32, UINT8, UINT8) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) override { return S_OK; }
	};
}

void FontReplacement::DirectWriteTextAnalyzer::Itemize(std::span<const wchar_t> text, std::vector<TextRun>& runs) {
	runs.clear();
	const auto length = text.size();
	if (length == 0)
		return;
	if (m_scripts.size() < length)
		m_scripts.resize((std::max)(length, m_scripts.size() * 2));

	TextAnalysisSource source(text.data(), static_cast<UINT32>(length));
	ScriptSink sink(std::span(m_scripts).first(length));
	ThrowOnError(m_services.Analyzer().AnalyzeScript(&source, 0, static_cast<UINT32>(length), &sink), "AnalyzeScript");

	size_t start = 0;
	auto rtl = false;
	for (size_t i = 0; i < length; i++) {
		const auto unitRtl = IS_LOW_SURROGATE(text[i]) && i > 0 ? rtl : IsRightToLeft(text, i);
		if (i != 0 && (unitRtl != rtl || ToToken(m_scripts[i]) != ToToken(m_scripts[start]))) {
			runs.push_back({static_cast<int>(start), static_cast<int>(i), ToToken(m_scripts[start]), rtl});
			start = i;
		}
		rtl = unitRtl;
	}

	runs.push_back({static_cast<int>(start), static_cast<int>(length), ToToken(m_scripts[start]), rtl});
}
