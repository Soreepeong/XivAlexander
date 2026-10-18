#include "pch.h"
#include "MainApp/FontReplacement/Fonts/Records/RasterGlyph.h"

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
	if (scale < 1) {
		const auto columns = AreaWeights(x0, w, left, scale, Width);
		const auto rows = AreaWeights(y0, h, top, scale, Height);
		std::vector<float> horizontal(static_cast<size_t>(w) * Height);
		for (auto y = 0; y < Height; y++) {
			const auto src = &Alpha[static_cast<size_t>(y) * Width];
			for (auto x = 0; x < w; x++) {
				auto sum = 0.f;
				for (const auto& [i, weight] : columns[x])
					sum += weight * static_cast<float>(src[i]);
				horizontal[static_cast<size_t>(y) * w + x] = sum;
			}
		}
		const auto norm = scale * scale;
		for (auto y = 0; y < h; y++) {
			for (auto x = 0; x < w; x++) {
				auto sum = 0.f;
				for (const auto& [j, weight] : rows[y])
					sum += weight * horizontal[static_cast<size_t>(j) * w + x];
				res.Alpha[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(std::round(sum * norm), 0.f, 255.f));
			}
		}
		return res;
	}

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

std::vector<std::vector<std::pair<int, float>>> FontReplacement::RasterGlyph::AreaWeights(int first, int count, float origin, float scale, int sourceSize) {
	std::vector<std::vector<std::pair<int, float>>> res(count);
	for (auto d = 0; d < count; d++) {
		const auto u0 = (static_cast<float>(first + d) - origin) / scale;
		const auto u1 = (static_cast<float>(first + d + 1) - origin) / scale;
		const auto i0 = (std::max)(0, static_cast<int>(std::floor(u0)));
		const auto i1 = (std::min)(sourceSize, static_cast<int>(std::ceil(u1)));
		for (auto i = i0; i < i1; i++) {
			const auto weight = (std::min)(u1, static_cast<float>(i + 1)) - (std::max)(u0, static_cast<float>(i));
			if (weight > 0)
				res[d].emplace_back(i, weight);
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
	const auto columns = AreaWeights(x0, w, static_cast<float>(Left) * scale, scale, Width);
	RasterGlyph res{Advance, x0, Top, w, Height, std::vector<uint8_t>(static_cast<size_t>(w) * Height)};
	for (auto y = 0; y < Height; y++) {
		const auto src = &Alpha[static_cast<size_t>(y) * Width];
		for (auto x = 0; x < w; x++) {
			auto sum = 0.f;
			for (const auto& [i, weight] : columns[x])
				sum += weight * static_cast<float>(src[i]);
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
	return Reboxed(Left + x1, Top + y1, x2 - x1, y2 - y1);
}

FontReplacement::RasterGlyph FontReplacement::RasterGlyph::Reboxed(int left, int top, int width, int height) const {
	RasterGlyph res{Advance, left, top, width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height)};
	if (HasLcd())
		res.Lcd.resize(res.Alpha.size() * 3);
	const auto x1 = (std::max)(left, Left), x2 = (std::min)(left + width, Left + Width);
	for (auto y = (std::max)(top, Top); y < (std::min)(top + height, Top + Height) && x1 < x2; y++) {
		const auto from = static_cast<size_t>(y - Top) * Width + (x1 - Left);
		const auto to = static_cast<size_t>(y - top) * width + (x1 - left);
		std::copy_n(Alpha.begin() + from, x2 - x1, res.Alpha.begin() + to);
		if (HasLcd())
			std::copy_n(Lcd.begin() + from * 3, (x2 - x1) * 3, res.Lcd.begin() + to * 3);
	}
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
	if (std::ranges::any_of(pieces, &RasterGlyph::HasLcd)) {
		res.Lcd.resize(res.Alpha.size() * 3);
		for (const auto& p : pieces)
			BlitMaxLcd(res.Lcd, w, h, p, p.Left - left, p.Top - top);
	}
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

void FontReplacement::RasterGlyph::BlitMaxLcd(std::span<uint8_t> buffer, int width, int height, const RasterGlyph& g, int x, int y) {
	for (auto row = (std::max)(0, -y); row < g.Height && row + y < height; row++) {
		for (auto col = (std::max)(0, -x); col < g.Width && col + x < width; col++) {
			const auto from = static_cast<size_t>(row) * g.Width + col;
			const auto d = &buffer[(static_cast<size_t>(row + y) * width + col + x) * 3];
			for (auto c = 0; c < 3; c++)
				d[c] = (std::max)(d[c], g.HasLcd() ? g.Lcd[from * 3 + c] : g.Alpha[from]);
		}
	}
}

std::vector<uint8_t> FontReplacement::RasterGlyph::GammaTable(float gamma) {
	return gamma != 1 && gamma > 0 ? CoverageTable(1 / gamma) : std::vector<uint8_t>{};
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
	for (auto& a : res.Lcd)
		a = table[a];
	return res;
}
