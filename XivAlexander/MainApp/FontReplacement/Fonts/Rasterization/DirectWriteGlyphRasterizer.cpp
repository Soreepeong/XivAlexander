#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Rasterization/DirectWriteGlyphRasterizer.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <FontChanger.Presets/ElementFonts.h>

#include "MainApp/FontReplacement/Fonts/Rasterization/FreeTypeGlyphRasterizer.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

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
	// Grayscale is the mean of the three subpixels; aliased text has no ClearType.
	const auto subpixel = UsesSubpixel(parameters);
	if (subpixel && IsVertical(m_subpixelOrder) && m_verticalWithFreeType) {
		if (auto drawn = RasterizeWithFreeType(face, px, glyphs, advances, offsets, count, originX, advance, parameters, transform, originY))
			return std::move(*drawn);
	}

	// ClearType's stripes are horizontal: for vertical ones, the glyphs are drawn turned a quarter and turned back.
	const auto turned = subpixel && IsVertical(m_subpixelOrder);
	const auto analysis = CreateAnalysis(face, px, glyphs, advances, offsets, count, originX, originY, parameters, transform,
		subpixel ? DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE : DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, turned);

	RECT bounds;
	if (subpixel) {
		ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds), "GetAlphaTextureBounds");
		auto w = bounds.right - bounds.left;
		auto h = bounds.bottom - bounds.top;
		if (w > 0 && h > 0) {
			std::vector<uint8_t> lcd(static_cast<size_t>(w) * h * 3);
			ThrowOnError(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, lcd.data(), static_cast<UINT32>(lcd.size())), "CreateAlphaTexture");
			if (turned) {
				lcd = TurnBack(lcd, w, h, 3);
				bounds = TurnBack(bounds);
				std::swap(w, h);
			}

			std::vector<uint8_t> alpha(static_cast<size_t>(w) * h);
			const auto blueFirst = IsBlueFirst(m_subpixelOrder);
			for (size_t i = 0; i < alpha.size(); i++) {
				auto& r = lcd[i * 3];
				auto& b = lcd[i * 3 + 2];
				alpha[i] = static_cast<uint8_t>((r + lcd[i * 3 + 1] + b + 1) / 3);
				if (blueFirst)
					std::swap(r, b);
			}
			return {advance, bounds.left, bounds.top, w, h, std::move(alpha), std::move(lcd)};
		}

		// A font may still render aliased (its recommended mode): the 1x1 texture has it then.
	}

	// With grayscale antialiasing, the 1x1 texture type gives 8-bit coverage.
	ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds), "GetAlphaTextureBounds");
	auto w = bounds.right - bounds.left;
	auto h = bounds.bottom - bounds.top;
	std::vector<uint8_t> alpha;
	if (w > 0 && h > 0) {
		alpha.resize(static_cast<size_t>(w) * h);
		ThrowOnError(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, alpha.data(), static_cast<UINT32>(alpha.size())), "CreateAlphaTexture");
		if (turned) {
			alpha = TurnBack(alpha, w, h, 1);
			bounds = TurnBack(bounds);
			std::swap(w, h);
		}
	} else {
		w = h = 0;
	}
	return {advance, bounds.left, bounds.top, w, h, std::move(alpha)};
}

RECT FontReplacement::DirectWriteGlyphRasterizer::GetRunBounds(
	IDWriteFontFace5* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	const DirectWriteParams& parameters,
	const Matrix2x2& transform) const {
	const auto subpixel = UsesSubpixel(parameters);
	const auto vertical = subpixel && IsVertical(m_subpixelOrder);
	if (vertical && m_verticalWithFreeType) {
		if (const auto g = RasterizeWithFreeType(face, px, glyphs, advances, offsets, count, 0, 0, parameters, transform, 0))
			return {.left = g->Left, .top = g->Top, .right = g->Left + g->Width, .bottom = g->Top + g->Height};
	}

	// The texture RasterizeRun draws: ClearType's (antialiased horizontally), unless the font renders aliased.
	const auto analysis = CreateAnalysis(face, px, glyphs, advances, offsets, count, 0, 0, parameters, transform,
		subpixel ? DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE : DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, vertical);
	RECT bounds{};
	if (subpixel)
		ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds), "GetAlphaTextureBounds");
	if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
		ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds), "GetAlphaTextureBounds");
	return vertical ? TurnBack(bounds) : bounds;
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

	// DirectWrite's oblique simulation slants as synthesis does, before the transformation; its bold simulation makes glyphs 1/50 em wider and taller.
	const auto simulations = face->GetSimulations();
	auto t = transform;
	if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
		t *= Matrix2x2{1, -FontChanger::ElementFonts::SynthesizedFace::ObliqueSlope, 0, 1};
	const auto embolden = simulations & DWRITE_FONT_SIMULATIONS_BOLD ? 1.f / 50 : 0.f;

	// GDI's modes hint both ways, the natural ones vertically only (FT_LOAD_TARGET_NORMAL, _LIGHT); with the font's own hinting, as DirectWrite
	// does: FreeType's light mode auto-hints TrueType fonts otherwise, which rounds heights differently (Comic Sans gains a row above and below).
	const FreeTypeParams ftParams{
		.LoadFlags = parameters.GridFitMode == DWRITE_GRID_FIT_MODE_DISABLED ? FT_LOAD_NO_HINTING : FT_LOAD_NO_AUTOHINT,
		.RenderMode = parameters.RenderMode == DWRITE_RENDERING_MODE1_GDI_CLASSIC || parameters.RenderMode == DWRITE_RENDERING_MODE1_GDI_NATURAL
			? FT_RENDER_MODE_NORMAL
			: FT_RENDER_MODE_LIGHT,
	};

	const auto axes = GetAxisValues(face);
	return m_freeType.RasterizeRun(ftFace, px, glyphs, advances, offsets, count, originX, advance, ftParams, t, axes ? &*axes : nullptr, embolden, originY);
}

std::optional<std::map<uint32_t, float>> FontReplacement::DirectWriteGlyphRasterizer::GetAxisValues(IDWriteFontFace5* face) {
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

std::vector<uint8_t> FontReplacement::DirectWriteGlyphRasterizer::TurnBack(const std::vector<uint8_t>& pixels, int width, int height, int channels) {
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

RECT FontReplacement::DirectWriteGlyphRasterizer::TurnBack(const RECT& turned) {
	// Drawn (u, v) is (y, -x) on screen.
	return {.left = -turned.bottom, .top = turned.left, .right = -turned.top, .bottom = turned.right};
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
	DWRITE_TEXT_ANTIALIAS_MODE antialiasMode,
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

	// Outlines can't be rasterized.
	if (renderingMode == DWRITE_RENDERING_MODE1_OUTLINE || renderingMode == DWRITE_RENDERING_MODE1_DEFAULT)
		renderingMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;

	auto gridFitMode = parameters.GridFitMode;

	// Turned, the other modes leave the text's horizontal unantialiased, and hint across it.
	if (turned) {
		renderingMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
		gridFitMode = DWRITE_GRID_FIT_MODE_DISABLED;
	}

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

		// Parameters DirectWrite refuses are tried again as the defaults.
		renderingMode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
		measuringMode = DWRITE_MEASURING_MODE_NATURAL;
		gridFitMode = DWRITE_GRID_FIT_MODE_DEFAULT;
	}
}
