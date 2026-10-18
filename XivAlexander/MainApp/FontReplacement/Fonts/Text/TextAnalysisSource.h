#pragma once

#include <dwrite_3.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// A text left to right in en-us, for the analyzer and the font fallback; lives where it is declared (no reference counting), the text with it.
	class TextAnalysisSource final : public IDWriteTextAnalysisSource {
		const wchar_t* m_text;
		UINT32 m_length;

	public:
		TextAnalysisSource(const wchar_t* text, UINT32 length)
			: m_text(text), m_length(length) {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text, UINT32* length) override;
		HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text, UINT32* length) override;
		DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
		HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length, const WCHAR** locale) override;
		HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) override;
	};
}
