#include "pch.h"
#include "XivAlexanderCommon/Sqex/FontCsv/BaseFont.h"

#include "XivAlexanderCommon/Sqex/Texture/Mipmap.h"

void Sqex::FontCsv::GlyphMeasurement::AdjustToIntersection(GlyphMeasurement& r, SSIZE_T srcWidth, SSIZE_T srcHeight, SSIZE_T destWidth, SSIZE_T destHeight) {
	if (left < 0) {
		r.left -= left;
		left = 0;
	}
	if (r.left < 0) {
		left -= r.left;
		r.left = 0;
	}
	if (top < 0) {
		r.top -= top;
		top = 0;
	}
	if (r.top < 0) {
		top -= r.top;
		r.top = 0;
	}
	if (right >= srcWidth) {
		r.right -= right - srcWidth;
		right = srcWidth;
	}
	if (r.right >= destWidth) {
		right -= r.right - destWidth;
		r.right = destWidth;
	}
	if (bottom >= srcHeight) {
		r.bottom -= bottom - srcHeight;
		bottom = srcHeight;
	}
	if (r.bottom >= destHeight) {
		bottom -= r.bottom - destHeight;
		r.bottom = destHeight;
	}

	if (left >= right || r.left >= r.right || top >= bottom || r.top >= r.bottom)
		*this = r = {};
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::BaseFont::MaxBoundingBox() const {
	if (!m_maxBoundingBox.empty)
		return m_maxBoundingBox;
	GlyphMeasurement res{false, INT_MAX, INT_MAX, INT_MIN, INT_MIN, 0};
	for (const auto& c : GetAllCharacters()) {
		GlyphMeasurement cur = Measure(0, 0, c);
		if (cur.empty)
			throw std::runtime_error("Character found from GetAllCharacters but GetBoundingBox returned empty");
		res.left = std::min(res.left, cur.left);
		res.top = std::min(res.top, cur.top);
		res.right = std::max(res.right, cur.right);
		res.bottom = std::max(res.bottom, cur.bottom);
	}
	m_maxBoundingBox = res;
	return res;
}

SSIZE_T Sqex::FontCsv::BaseFont::GetKerning(char32_t l, char32_t r, SSIZE_T defaultOffset) const {
	if (!l || !r)
		return defaultOffset;

	const auto& t = GetKerningTable();
	const auto it = t.find(std::make_pair(l, r));
	if (it == t.end())
		return defaultOffset;
	return it->second;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::BaseFont::Measure(SSIZE_T x, SSIZE_T y, const std::u32string& s) const {
	if (s.empty())
		return {};

	char32_t lastChar = 0;
	const auto iHeight = static_cast<SSIZE_T>(LineHeight());

	GlyphMeasurement result{};
	SSIZE_T currX = x, currY = y;

	for (const auto currChar : s) {
		if (currChar == u'\r') {
			continue;
		} else if (currChar == u'\n') {
			currX = x;
			currY += iHeight;
			lastChar = 0;
			continue;
		} else if (currChar == u'\u200c') {
			// unicode non-joiner
			lastChar = 0;
			continue;
		}

		const auto kerning = GetKerning(lastChar, currChar);
		const auto currBbox = Measure(currX + kerning, currY, currChar);
		currX += kerning + currBbox.advanceX;
		result.ExpandToFit(currBbox);
		lastChar = currChar;
	}
	if (result.empty)
		return {true};

	return result;
}
