#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/DirectWriteGlyphRasterizer.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <FontChanger.Presets/ElementFonts.h>

#include "MainApp/FontReplacement/Fonts/Rasterization/FreeTypeGlyphRasterizer.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

namespace {
	// A font may still render aliased (its recommended mode), leaving the ClearType texture empty; the 1x1 texture holds 8-bit coverage when antialiased.
	std::pair<DWRITE_TEXTURE_TYPE, RECT> GetTexture(IDWriteGlyphRunAnalysis* analysis, bool subpixel) {
		RECT bounds{};
		if (subpixel) {
			FontReplacement::ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds), "GetAlphaTextureBounds");
			if (bounds.right > bounds.left && bounds.bottom > bounds.top)
				return {DWRITE_TEXTURE_CLEARTYPE_3x1, bounds};
		}

		FontReplacement::ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds), "GetAlphaTextureBounds");
		return {DWRITE_TEXTURE_ALIASED_1x1, bounds};
	}

	std::optional<std::map<uint32_t, float>> GetAxisValues(IDWriteFontFace5* face) {
		if (!face->HasVariations())
			return std::nullopt;

		std::vector<DWRITE_FONT_AXIS_VALUE> values(face->GetFontAxisValueCount());
		if (FAILED(face->GetFontAxisValues(values.data(), static_cast<UINT32>(values.size()))))
			return std::nullopt;

		std::map<uint32_t, float> res;
		for (const auto& v : values)
			res[static_cast<uint32_t>(v.axisTag)] = v.value;
		return res;
	}

	std::vector<uint8_t> TurnBack(const std::vector<uint8_t>& pixels, int width, int height, int channels) {
		std::vector<uint8_t> res(pixels.size());
		for (auto y = 0; y < height; y++) {
			for (auto x = 0; x < width; x++) {
				const auto from = (static_cast<size_t>(y) * width + x) * channels;
				const auto to = (static_cast<size_t>(x) * height + (height - 1 - y)) * channels;
				std::copy_n(&pixels[from], channels, &res[to]);
			}
		}
		return res;
	}

	RECT TurnBack(const RECT& turned) {
		// (u, v) drawn turned is (y, -x) on screen.
		return {.left = -turned.bottom, .top = turned.left, .right = -turned.top, .bottom = turned.right};
	}
}

FontReplacement::RasterGlyph FontReplacement::DirectWriteGlyphRasterizer::RasterizeRun(
	IDWriteFontFace5* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	const DirectWriteParams& parameters,
	const Matrix2x2& transform,
	float originY) const {
	const auto subpixel = UsesSubpixel(parameters);
	const auto turned = subpixel && IsVertical(m_subpixelOrder);
	if (turned && m_verticalWithFreeType) {
		if (auto drawn = RasterizeWithFreeType(face, px, glyphs, advances, offsets, count, originX, advance, parameters, transform, originY))
			return std::move(*drawn);
	}

	const auto analysis = CreateAnalysis(face, px, glyphs, advances, offsets, count, originX, originY, parameters, transform, subpixel, turned);
	auto [type, bounds] = GetTexture(analysis, subpixel);
	auto w = bounds.right - bounds.left;
	auto h = bounds.bottom - bounds.top;
	if (w <= 0 || h <= 0)
		return {advance, bounds.left, bounds.top, 0, 0, {}};

	const auto channels = type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
	std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * channels);
	ThrowOnError(analysis->CreateAlphaTexture(type, &bounds, pixels.data(), static_cast<UINT32>(pixels.size())), "CreateAlphaTexture");
	if (turned) {
		pixels = TurnBack(pixels, w, h, channels);
		bounds = TurnBack(bounds);
		std::swap(w, h);
	}

	if (channels == 1)
		return {advance, bounds.left, bounds.top, w, h, std::move(pixels)};
	auto alpha = FreeTypeGlyphRasterizer::TakeSubpixelMean(pixels, IsBlueFirst(m_subpixelOrder));
	return {advance, bounds.left, bounds.top, w, h, std::move(alpha), std::move(pixels)};
}

bool FontReplacement::DirectWriteGlyphRasterizer::UsesSubpixel(const DirectWriteParams& parameters) const {
	return m_subpixelOrder != SubpixelOrder::None && parameters.RenderMode != DWRITE_RENDERING_MODE1_ALIASED;
}

std::optional<FontReplacement::RasterGlyph> FontReplacement::DirectWriteGlyphRasterizer::RasterizeWithFreeType(
	IDWriteFontFace5* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	const DirectWriteParams& parameters,
	const Matrix2x2& transform,
	float originY) const {
	const auto ftFace = m_freeType.Open(face);
	if (!ftFace || !FreeTypeGlyphRasterizer::IsScalable(ftFace))
		return std::nullopt;

	// DirectWrite's oblique simulation slants before the transformation; its bold simulation adds 1/50 em both ways.
	const auto simulations = face->GetSimulations();
	auto t = transform;
	if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
		t *= Matrix2x2{1, -FontChanger::ElementFonts::SynthesizedFace::ObliqueSlope, 0, 1};
	const auto embolden = simulations & DWRITE_FONT_SIMULATIONS_BOLD ? 1.f / 50 : 0.f;

	// GDI's modes hint both ways and the natural ones vertically only, always with the font's own hinting as DirectWrite does, since FreeType's light
	// mode would auto-hint TrueType fonts and round heights differently.
	const FreeTypeParams ftParams{
		.LoadFlags = parameters.GridFitMode == DWRITE_GRID_FIT_MODE_DISABLED ? FT_LOAD_NO_HINTING : FT_LOAD_NO_AUTOHINT,
		.RenderMode = parameters.RenderMode == DWRITE_RENDERING_MODE1_GDI_CLASSIC || parameters.RenderMode == DWRITE_RENDERING_MODE1_GDI_NATURAL
			? FT_RENDER_MODE_NORMAL
			: FT_RENDER_MODE_LIGHT,
	};

	const auto axes = GetAxisValues(face);
	return m_freeType.RasterizeRun(ftFace, px, glyphs, advances, offsets, count, originX, advance, ftParams, t, axes ? &*axes : nullptr, embolden, originY);
}

IDWriteGlyphRunAnalysisPtr FontReplacement::DirectWriteGlyphRasterizer::CreateAnalysis(
	IDWriteFontFace5* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	float originY,
	const DirectWriteParams& parameters,
	const Matrix2x2& transform,
	bool subpixel,
	bool turned) const {
	// DirectWrite transforms row vectors; the origin is on screen, after the transformation.
	auto matrix = DWRITE_MATRIX{transform.M11, transform.M21, transform.M12, transform.M22, originX, originY};
	if (turned)
		matrix = {matrix.m12, -matrix.m11, matrix.m22, -matrix.m21, matrix.dy, -matrix.dx};

	const DWRITE_GLYPH_RUN run{
		.fontFace = face,
		.fontEmSize = px,
		.glyphCount = count,
		.glyphIndices = glyphs,
		.glyphAdvances = advances,
		.glyphOffsets = offsets,
		.isSideways = FALSE,
		.bidiLevel = 0,
	};

	auto measuringMode = parameters.MeasureMode;
	auto renderingMode = parameters.RenderMode;
	if (renderingMode == DWRITE_RENDERING_MODE1_DEFAULT) {
		DWRITE_RENDERING_MODE recommended;
		static_cast<IDWriteFontFace*>(face)->GetRecommendedRenderingMode(px, 1, measuringMode, nullptr, &recommended);
		renderingMode = static_cast<DWRITE_RENDERING_MODE1>(recommended);
	}

	// Glyph run analysis doesn't rasterize outlines.
	if (renderingMode == DWRITE_RENDERING_MODE1_OUTLINE || renderingMode == DWRITE_RENDERING_MODE1_DEFAULT)
		renderingMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;

	auto gridFitMode = parameters.GridFitMode;

	// Turned, the other modes would leave the text's horizontal unantialiased, and hint across it.
	if (turned) {
		renderingMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
		gridFitMode = DWRITE_GRID_FIT_MODE_DISABLED;
	}

	const auto antialiasMode = subpixel ? DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE : DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
	for (auto attempt = 0;; attempt++) {
		IDWriteGlyphRunAnalysisPtr analysis;
		const auto hr = m_factory->CreateGlyphRunAnalysis(
			&run,
			&matrix,
			renderingMode,
			measuringMode,
			gridFitMode,
			antialiasMode,
			0,
			0,
			&analysis);
		if (SUCCEEDED(hr))
			return analysis;
		if (attempt != 0)
			ThrowOnError(hr, "CreateGlyphRunAnalysis");

		// DirectWrite refuses some combinations of modes, which the defaults avoid.
		renderingMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
		measuringMode = DWRITE_MEASURING_MODE_NATURAL;
		gridFitMode = DWRITE_GRID_FIT_MODE_DEFAULT;
	}
}
