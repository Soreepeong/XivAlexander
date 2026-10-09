#pragma once

#include <dwrite_3.h>

namespace XivAlexander::Apps::MainApp::FontReplacement {
	// Receives the glyph runs of a text layout (GlyphRunCollector).
	using GlyphRunSink = std::function<void(float baselineX, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description)>;

	// An IDWriteTextRenderer that draws nothing: IDWriteTextLayout::Draw hands it each shaped glyph run (one font face and
	// script each, fallback applied), which it passes to the sink given as the drawing context. Not reference counted (it
	// lives as long as its owner).
	class GlyphRunCollector final : public IDWriteTextRenderer {
	public:
		// Draws a layout into a sink.
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
