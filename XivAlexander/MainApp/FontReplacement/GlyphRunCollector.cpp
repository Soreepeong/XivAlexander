#include "pch.h"
#include "MainApp/FontReplacement/GlyphRunCollector.h"

#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::GlyphRunCollector::Collect(IDWriteTextLayout* layout, GlyphRunSink sink) {
	ThrowOnError(layout->Draw(&sink, this, 0, 0), "Collecting the glyph runs of a layout");
}

HRESULT FontReplacement::GlyphRunCollector::QueryInterface(REFIID riid, void** ppv) {
	if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IDWriteTextRenderer)) {
		*ppv = this;
		return S_OK;
	}

	*ppv = nullptr;
	return E_NOINTERFACE;
}

HRESULT FontReplacement::GlyphRunCollector::IsPixelSnappingDisabled(void* context, BOOL* isDisabled) {
	// Positions stay fractional; the cluster cells carry the fraction.
	*isDisabled = TRUE;
	return S_OK;
}

HRESULT FontReplacement::GlyphRunCollector::GetCurrentTransform(void* context, DWRITE_MATRIX* transform) {
	*transform = {1, 0, 0, 1, 0, 0};
	return S_OK;
}

HRESULT FontReplacement::GlyphRunCollector::GetPixelsPerDip(void* context, FLOAT* pixelsPerDip) {
	*pixelsPerDip = 1;
	return S_OK;
}

HRESULT FontReplacement::GlyphRunCollector::DrawGlyphRun(void* context, FLOAT baselineX, FLOAT baselineY, DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description, IUnknown* effect) {
	try {
		(*static_cast<GlyphRunSink*>(context))(baselineX, run, description);
		return S_OK;
	} catch (const std::exception& e) {
		Host::Error("Collecting a glyph run failed: {}", e.what());
		return E_FAIL;
	}
}
