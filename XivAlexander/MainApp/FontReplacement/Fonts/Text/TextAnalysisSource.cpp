#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Text/TextAnalysisSource.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

HRESULT FontReplacement::TextAnalysisSource::QueryInterface(REFIID riid, void** ppv) {
	if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSource)) {
		*ppv = static_cast<IDWriteTextAnalysisSource*>(this);
		return S_OK;
	}

	*ppv = nullptr;
	return E_NOINTERFACE;
}

HRESULT FontReplacement::TextAnalysisSource::GetTextAtPosition(UINT32 position, const WCHAR** text, UINT32* length) {
	const auto inside = position < m_length;
	*text = inside ? m_text + position : nullptr;
	*length = inside ? m_length - position : 0;
	return S_OK;
}

HRESULT FontReplacement::TextAnalysisSource::GetTextBeforePosition(UINT32 position, const WCHAR** text, UINT32* length) {
	const auto inside = position != 0 && position <= m_length;
	*text = inside ? m_text : nullptr;
	*length = inside ? position : 0;
	return S_OK;
}

HRESULT FontReplacement::TextAnalysisSource::GetLocaleName(UINT32 position, UINT32* length, const WCHAR** locale) {
	*length = position < m_length ? m_length - position : 0;
	*locale = L"en-us";
	return S_OK;
}

HRESULT FontReplacement::TextAnalysisSource::GetNumberSubstitution(UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) {
	*length = position < m_length ? m_length - position : 0;
	*substitution = nullptr;
	return S_OK;
}
