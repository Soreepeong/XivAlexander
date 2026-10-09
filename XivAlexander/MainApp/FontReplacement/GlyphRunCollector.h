#pragma once

#include <dwrite_3.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	using GlyphRunSink = std::function<void(float baselineX, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description)>;

	// Draws nothing: passes each shaped run (one face and script, fallback applied) to the sink given as drawing context. Not ref-counted.
	class GlyphRunCollector final : public IDWriteTextRenderer {
	public:
		void Collect(IDWriteTextLayout* layout, GlyphRunSink sink);

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void* context, BOOL* isDisabled) override;
		HRESULT STDMETHODCALLTYPE GetCurrentTransform(void* context, DWRITE_MATRIX* transform) override;
		HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void* context, FLOAT* pixelsPerDip) override;
		HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* context, FLOAT baselineX, FLOAT baselineY, DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description, IUnknown* effect) override;
		HRESULT STDMETHODCALLTYPE DrawUnderline(void* context, FLOAT x, FLOAT y, const DWRITE_UNDERLINE* underline, IUnknown* effect) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE DrawStrikethrough(void* context, FLOAT x, FLOAT y, const DWRITE_STRIKETHROUGH* strikethrough, IUnknown* effect) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE DrawInlineObject(void* context, FLOAT x, FLOAT y, IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft, IUnknown* effect) override { return S_OK; }
	};
}
