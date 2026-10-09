#include "pch.h"
#include "MainApp/FontReplacement/GlyphRasterizer.h"

#include "MainApp/FontReplacement/FreeTypeFonts.h"
#include "MainApp/FontReplacement/Host.h"
#include "MainApp/FontReplacement/Utilities.h"

namespace FontReplacement = XivAlexander::Apps::MainApp::FontReplacement;

void FontReplacement::RasterGlyph::ScaledBounds(int left, int top, int width, int height, float scale, float shiftX, float shiftY, int& x0, int& y0, int& w, int& h) {
	x0 = static_cast<int>(std::floor(static_cast<float>(left) * scale + shiftX));
	y0 = static_cast<int>(std::floor(static_cast<float>(top) * scale + shiftY));
	w = static_cast<int>(std::ceil(static_cast<float>(left + width) * scale + shiftX)) - x0;
	h = static_cast<int>(std::ceil(static_cast<float>(top + height) * scale + shiftY)) - y0;
}

FontReplacement::RasterGlyph FontReplacement::RasterGlyph::Scaled(float scale, float shiftX, float shiftY) const {
	int x0, y0, w, h;
	ScaledBounds(Left, Top, Width, Height, scale, shiftX, shiftY, x0, y0, w, h);
	const auto left = static_cast<float>(Left) * scale + shiftX;
	const auto top = static_cast<float>(Top) * scale + shiftY;
	RasterGlyph res{Advance, x0, y0, w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h)};
	constexpr int Samples = 4;
	for (auto y = 0; y < h; y++) {
		for (auto x = 0; x < w; x++) {
			auto sum = 0.f;
			for (auto sy = 0; sy < Samples; sy++) {
				for (auto sx = 0; sx < Samples; sx++) {
					// The sample's position in source pixels, from their centers.
					const auto u = (static_cast<float>(x0 + x) + (static_cast<float>(sx) + 0.5f) / Samples - left) / scale - 0.5f;
					const auto v = (static_cast<float>(y0 + y) + (static_cast<float>(sy) + 0.5f) / Samples - top) / scale - 0.5f;
					sum += Bilinear(u, v);
				}
			}
			res.Alpha[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::round(sum / (Samples * Samples)));
		}
	}
	return res;
}

float FontReplacement::RasterGlyph::Bilinear(float u, float v) const {
	const auto x = static_cast<int>(std::floor(u));
	const auto y = static_cast<int>(std::floor(v));
	const auto fx = u - static_cast<float>(x), fy = v - static_cast<float>(y);
	const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
	return lerp(lerp(At(x, y), At(x + 1, y), fx), lerp(At(x, y + 1), At(x + 1, y + 1), fx), fy);
}

float FontReplacement::RasterGlyph::At(int x, int y) const {
	return x < 0 || y < 0 || x >= Width || y >= Height ? 0.f : static_cast<float>(Alpha[static_cast<size_t>(y) * Width + x]);
}

FontReplacement::RasterGlyph FontReplacement::RasterGlyph::SqueezedX(float scale) const {
	const auto x0 = static_cast<int>(std::floor(static_cast<float>(Left) * scale));
	const auto w = (std::max)(1, static_cast<int>(std::ceil(static_cast<float>(Left + Width) * scale)) - x0);
	RasterGlyph res{Advance, x0, Top, w, Height, std::vector<uint8_t>(static_cast<size_t>(w) * Height)};
	for (auto y = 0; y < Height; y++) {
		for (auto x = 0; x < w; x++) {
			const auto u0 = static_cast<float>(x0 + x) / scale - static_cast<float>(Left);
			const auto u1 = static_cast<float>(x0 + x + 1) / scale - static_cast<float>(Left);
			auto sum = 0.f;
			for (auto u = static_cast<int>(std::floor(u0)); static_cast<float>(u) < std::ceil(u1); u++) {
				if (u < 0 || u >= Width)
					continue;
				const auto cover = (std::min)(static_cast<float>(u + 1), u1) - (std::max)(static_cast<float>(u), u0);
				sum += static_cast<float>(Alpha[static_cast<size_t>(y) * Width + u]) * cover;
			}
			res.Alpha[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(std::round(sum * scale), 0.f, 255.f));
		}
	}
	return res;
}

FontReplacement::RasterGlyph FontReplacement::RasterGlyph::Trimmed() const {
	int x1 = Width, y1 = Height, x2 = 0, y2 = 0;
	for (auto y = 0; y < Height; y++) {
		for (auto x = 0; x < Width; x++) {
			if (Alpha[static_cast<size_t>(y) * Width + x]) {
				x1 = (std::min)(x1, x), y1 = (std::min)(y1, y);
				x2 = (std::max)(x2, x + 1), y2 = (std::max)(y2, y + 1);
			}
		}
	}

	if (x1 >= x2 || y1 >= y2)
		return {Advance, 0, 0, 0, 0, {}};
	const auto w = x2 - x1;
	RasterGlyph res{Advance, Left + x1, Top + y1, w, y2 - y1, std::vector<uint8_t>(static_cast<size_t>(w) * (y2 - y1))};
	for (auto y = y1; y < y2; y++)
		std::copy_n(&Alpha[static_cast<size_t>(y) * Width + x1], w, &res.Alpha[static_cast<size_t>(y - y1) * w]);
	return res;
}

FontReplacement::RasterGlyph FontReplacement::RasterGlyph::Merge(std::vector<RasterGlyph> pieces, int advance) {
	std::erase_if(pieces, [](const RasterGlyph& p) { return p.Width == 0; });
	if (pieces.empty())
		return {advance, 0, 0, 0, 0, {}};
	if (pieces.size() == 1) {
		auto res = std::move(pieces[0]);
		res.Advance = advance;
		return res;
	}

	auto left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
	for (const auto& p : pieces) {
		left = (std::min)(left, p.Left), top = (std::min)(top, p.Top);
		right = (std::max)(right, p.Left + p.Width), bottom = (std::max)(bottom, p.Top + p.Height);
	}
	const auto w = right - left;
	const auto h = bottom - top;
	RasterGlyph res{advance, left, top, w, h, std::vector<uint8_t>(static_cast<size_t>(w) * h)};
	for (const auto& p : pieces)
		BlitMax(res.Alpha, w, h, p, p.Left - left, p.Top - top);
	return res;
}

void FontReplacement::RasterGlyph::BlitMax(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y) {
	for (auto row = (std::max)(0, -y); row < g.Height && row + y < height; row++) {
		for (auto col = (std::max)(0, -x); col < g.Width && col + x < width; col++) {
			auto& d = buffer[static_cast<size_t>(row + y) * width + col + x];
			d = (std::max)(d, g.Alpha[static_cast<size_t>(row) * g.Width + col]);
		}
	}
}

std::vector<uint8_t> FontReplacement::RasterGlyph::CoverageTable(float exponent) {
	std::vector<uint8_t> table(256);
	for (auto i = 0; i < 256; i++)
		table[i] = static_cast<uint8_t>(std::round(255.f * std::pow(static_cast<float>(i) / 255.f, exponent)));
	return table;
}

FontReplacement::RasterGlyph FontReplacement::RasterGlyph::WithCoverage(const std::vector<uint8_t>& table) const {
	auto res = *this;
	for (auto& a : res.Alpha)
		a = table[a];
	return res;
}

FontReplacement::FontFileKey FontReplacement::FontFileKey::Of(IDWriteFontFace* face) {
	FontFileKey res;
	UINT32 count = 1;
	IDWriteFontFile* file = nullptr;
	if (FAILED(face->GetFiles(&count, &file)) || !file)
		return res;
	res.File.Attach(file);
	if (FAILED(res.File->GetReferenceKey(&res.Key, &res.KeySize)) || FAILED(res.File->GetLoader(&res.Loader)))
		return {};
	return res;
}

FontReplacement::GlyphRasterizer::GlyphRasterizer() {
	ThrowOnError(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(&m_factory)), "DWriteCreateFactory");
	ThrowOnError(m_factory->GetSystemFontCollection(&m_systemFonts, FALSE), "GetSystemFontCollection");

	IDWriteFontFallbackBuilderPtr builder;
	ThrowOnError(m_factory->CreateFontFallbackBuilder(&builder), "CreateFontFallbackBuilder");
	ThrowOnError(builder->CreateFontFallback(&m_noFallback), "CreateFontFallback");

	m_freeType = FreeTypeFonts::Create();
}

FontReplacement::GlyphRasterizer::~GlyphRasterizer() = default;

IDWriteFontPtr FontReplacement::GlyphRasterizer::FindFont(const std::string& name, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style) const {
	const auto wname = xivres::util::unicode::convert<std::wstring>(name);
	UINT32 index;
	BOOL exists;
	ThrowOnError(m_systemFonts->FindFamilyName(wname.c_str(), &index, &exists), "FindFamilyName");
	if (!exists)
		return nullptr;

	IDWriteFontFamilyPtr family;
	ThrowOnError(m_systemFonts->GetFontFamily(index, &family), "GetFontFamily");
	IDWriteFontPtr font;
	ThrowOnError(family->GetFirstMatchingFont(weight, stretch, style, &font), "GetFirstMatchingFont");
	return font;
}

IDWriteTextFormat1Ptr FontReplacement::GlyphRasterizer::CreateFormat(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, float size, IDWriteFontFallback* fallback) const {
	const auto wfamily = xivres::util::unicode::convert<std::wstring>(family);
	IDWriteTextFormatPtr format;
	ThrowOnError(m_factory->CreateTextFormat(wfamily.c_str(), nullptr, weight, style, stretch, size, L"en-us", &format), "CreateTextFormat");
	ThrowOnError(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "SetWordWrapping");
	IDWriteTextFormat1Ptr format1;
	ThrowOnError(format.QueryInterface(__uuidof(IDWriteTextFormat1), &format1), "IDWriteTextFormat1");
	ThrowOnError(format1->SetFontFallback(fallback), "SetFontFallback");
	return format1;
}

FontReplacement::RasterGlyph FontReplacement::GlyphRasterizer::RasterizeRun(
	IDWriteFontFace* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	int advance,
	const DirectWriteParams& parameters,
	const GlyphTransform& transform,
	float originY) const {
	const auto analysis = CreateAnalysis(face, px, glyphs, advances, offsets, count, originX, originY, parameters, transform);

	// With grayscale antialiasing, the 1x1 texture type gives 8-bit coverage.
	RECT bounds;
	ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds), "GetAlphaTextureBounds");
	auto w = bounds.right - bounds.left;
	auto h = bounds.bottom - bounds.top;
	std::vector<uint8_t> alpha;
	if (w > 0 && h > 0) {
		alpha.resize(static_cast<size_t>(w) * h);
		ThrowOnError(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, alpha.data(), static_cast<UINT32>(alpha.size())), "CreateAlphaTexture");
	} else {
		w = h = 0;
	}
	return {advance, bounds.left, bounds.top, w, h, std::move(alpha)};
}

RECT FontReplacement::GlyphRasterizer::GetRunBounds(
	IDWriteFontFace* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	const DirectWriteParams& parameters,
	const GlyphTransform& transform,
	float originX,
	float originY) const {
	const auto analysis = CreateAnalysis(face, px, glyphs, advances, offsets, count, originX, originY, parameters, transform);
	RECT bounds;
	ThrowOnError(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds), "GetAlphaTextureBounds");
	return bounds;
}

IDWriteGlyphRunAnalysisPtr FontReplacement::GlyphRasterizer::CreateAnalysis(
	IDWriteFontFace* face,
	float px,
	const uint16_t* glyphs,
	const float* advances,
	const DWRITE_GLYPH_OFFSET* offsets,
	uint32_t count,
	float originX,
	float originY,
	const DirectWriteParams& parameters,
	const GlyphTransform& transform) const {
	// DirectWrite transforms row vectors; the origin is on screen, after the transformation.
	const DWRITE_MATRIX matrix{transform.M11, transform.M21, transform.M12, transform.M22, originX, originY};

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
	if (renderingMode == DWRITE_RENDERING_MODE_DEFAULT)
		face->GetRecommendedRenderingMode(px, 1, measuringMode, nullptr, &renderingMode);

	// Outlines can't be rasterized.
	if (renderingMode == DWRITE_RENDERING_MODE_OUTLINE || renderingMode == DWRITE_RENDERING_MODE_DEFAULT)
		renderingMode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;

	auto gridFitMode = parameters.GridFitMode;
	for (auto attempt = 0;; attempt++) {
		IDWriteGlyphRunAnalysisPtr analysis;
		const auto hr = m_factory->CreateGlyphRunAnalysis(
			&run,
			&matrix,
			renderingMode,
			measuringMode,
			gridFitMode,
			DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
			0,
			0,
			&analysis);
		if (SUCCEEDED(hr))
			return analysis;
		if (attempt != 0)
			ThrowOnError(hr, "CreateGlyphRunAnalysis");

		// Parameters DirectWrite refuses are tried again as the defaults.
		renderingMode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
		measuringMode = DWRITE_MEASURING_MODE_NATURAL;
		gridFitMode = DWRITE_GRID_FIT_MODE_DEFAULT;
	}
}
