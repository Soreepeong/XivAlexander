#include "pch.h"
#include "Sqex_FontCsv_SeCompatibleFont.h"

#include "Sqex_Texture_Mipmap.h"

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

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeCompatibleFont::MaxBoundingBox() const {
	if (!m_maxBoundingBox.empty)
		return m_maxBoundingBox;
	GlyphMeasurement res{ false, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN };
	for (const auto& c : GetAllCharacters()) {
		GlyphMeasurement cur = Measure(0, 0, c);
		if (cur.empty)
			throw std::runtime_error("Character found from GetAllCharacters but GetBoundingBox returned empty");
		res.left = std::min(res.left, cur.left);
		res.top = std::min(res.top, cur.top);
		res.right = std::max(res.right, cur.right);
		res.bottom = std::max(res.bottom, cur.bottom);
		res.offsetX = std::max(res.offsetX, cur.offsetX);
	}
	m_maxBoundingBox = res;
	return res;
}

SSIZE_T Sqex::FontCsv::SeCompatibleFont::GetKerning(char32_t l, char32_t r, SSIZE_T defaultOffset) const {
	if (!l || !r)
		return defaultOffset;

	const auto& t = GetKerningTable();
	const auto it = t.find(std::make_pair(l, r));
	if (it == t.end())
		return defaultOffset;
	return it->second;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeCompatibleFont::Measure(SSIZE_T x, SSIZE_T y, const std::u32string& s) const {
	if (s.empty())
		return {};

	char32_t lastChar = 0;
	const auto iHeight = static_cast<SSIZE_T>(Height());

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
		} else if (currChar == u'\u200c') {  // unicode non-joiner
			lastChar = 0;
			continue;
		}

		const auto kerning = GetKerning(lastChar, currChar);
		const auto currBbox = Measure(currX + kerning, currY, currChar);
		if (!currBbox.empty) {
			if (result.empty) {
				result = currBbox;
				result.offsetX = result.right + result.offsetX;
			} else {
				result.left = std::min(result.left, currBbox.left);
				result.top = std::min(result.top, currBbox.top);
				result.right = std::max(result.right, currBbox.right);
				result.bottom = std::max(result.bottom, currBbox.bottom);
				result.offsetX = std::max(result.offsetX, currBbox.right + currBbox.offsetX);
			}
			currX = currBbox.right + currBbox.offsetX;
		}
		lastChar = currChar;
	}
	if (result.empty)
		return { true };

	result.offsetX -= result.right;
	return result;
}

struct Sqex::FontCsv::SeFont::Implementation {
	const std::shared_ptr<const ModifiableFontCsvStream> m_stream;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, SSIZE_T> m_kerningMap;

	mutable bool m_characterListDiscovered = false;
	mutable std::vector<char32_t> m_characterList;

	Implementation(std::shared_ptr<const ModifiableFontCsvStream> stream)
		: m_stream(std::move(stream)) {
	}
};

Sqex::FontCsv::SeFont::SeFont(std::shared_ptr<const ModifiableFontCsvStream> stream)
	: m_pImpl(std::make_unique<Implementation>(std::move(stream))) {
}

Sqex::FontCsv::SeFont::~SeFont() = default;

bool Sqex::FontCsv::SeFont::HasCharacter(char32_t c) const {
	return m_pImpl->m_stream->GetFontEntry(c);
}

SSIZE_T Sqex::FontCsv::SeFont::GetCharacterWidth(char32_t c) const {
	const auto entry = m_pImpl->m_stream->GetFontEntry(c);
	if (!entry)
		return {};

	return static_cast<SSIZE_T>(0) + entry->BoundingWidth + entry->NextOffsetX;
}

float Sqex::FontCsv::SeFont::Size() const {
	return m_pImpl->m_stream->Points();
}

const std::vector<char32_t>& Sqex::FontCsv::SeFont::GetAllCharacters() const {
	if (!m_pImpl->m_characterListDiscovered) {
		std::vector<char32_t> result;
		for (const auto& c : m_pImpl->m_stream->GetFontTableEntries())
			result.push_back(c.Char());
		m_pImpl->m_characterList = std::move(result);
		m_pImpl->m_characterListDiscovered = true;
		return m_pImpl->m_characterList;
	}
	return m_pImpl->m_characterList;
}

uint32_t Sqex::FontCsv::SeFont::Ascent() const {
	return m_pImpl->m_stream->Ascent();
}

uint32_t Sqex::FontCsv::SeFont::Descent() const {
	return m_pImpl->m_stream->LineHeight() - m_pImpl->m_stream->Ascent();
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::SeFont::GetKerningTable() const {
	if (!m_pImpl->m_kerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, SSIZE_T> result;
		for (const auto& k : m_pImpl->m_stream->GetKerningEntries()) {
			if (k.RightOffset)
				result.emplace(std::make_pair(k.Left(), k.Right()), k.RightOffset);
		}
		m_pImpl->m_kerningMap = std::move(result);
		m_pImpl->m_kerningDiscovered = true;
	}
	return m_pImpl->m_kerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeFont::Measure(SSIZE_T x, SSIZE_T y, const FontTableEntry & entry) {
	return {
		.empty = false,
		.left = x,
		.top = y + entry.CurrentOffsetY,
		.right = x + entry.BoundingWidth,
		.bottom = y + entry.CurrentOffsetY + entry.BoundingHeight,
		.offsetX = entry.NextOffsetX,
	};
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::SeFont::Measure(SSIZE_T x, SSIZE_T y, const char32_t c) const {
	const auto entry = GetStream().GetFontEntry(c);
	if (!entry)  // skip missing characters
		return {};
	return this->Measure(x, y, *entry);
}

const Sqex::FontCsv::ModifiableFontCsvStream& Sqex::FontCsv::SeFont::GetStream() const {
	return *m_pImpl->m_stream;
}

struct Sqex::FontCsv::CascadingFont::Implementation {
	const std::vector<std::shared_ptr<SeCompatibleFont>> m_fontList;
	const float m_size;
	const uint32_t m_ascent;
	const uint32_t m_descent;

	mutable bool m_kerningDiscovered = false;
	mutable std::map<std::pair<char32_t, char32_t>, SSIZE_T> m_kerningMap;

	mutable bool m_characterListDiscovered = false;
	mutable std::vector<char32_t> m_characterList;

	Implementation(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t descent)
		: m_fontList(std::move(fontList))
		, m_size(static_cast<bool>(normalizedSize) ? normalizedSize : std::ranges::max(m_fontList, {}, [](const auto& r) {return r->Size();  })->Size())
		, m_ascent(ascent != UINT32_MAX ? ascent : std::ranges::max(m_fontList, {}, [](const auto& r) {return r->Ascent();  })->Ascent())
		, m_descent(descent != UINT32_MAX ? descent : std::ranges::max(m_fontList, {}, [](const auto& r) {return r->Descent();  })->Descent()) {
	}

	size_t GetCharacterOwnerIndex(char32_t c) const {
		for (size_t i = 0; i < m_fontList.size(); ++i)
			if (m_fontList[i]->HasCharacter(c))
				return i;
		return SIZE_MAX;
	}
};

Sqex::FontCsv::CascadingFont::CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList)
	: m_pImpl(std::make_unique<Implementation>(std::move(fontList), 0.f, UINT32_MAX, UINT32_MAX)) {
}

Sqex::FontCsv::CascadingFont::CascadingFont(std::vector<std::shared_ptr<SeCompatibleFont>> fontList, float normalizedSize, uint32_t ascent, uint32_t descent)
	: m_pImpl(std::make_unique<Implementation>(std::move(fontList), normalizedSize, ascent, descent)) {
}

Sqex::FontCsv::CascadingFont::~CascadingFont() = default;

bool Sqex::FontCsv::CascadingFont::HasCharacter(char32_t c) const {
	return std::ranges::any_of(m_pImpl->m_fontList, [c](const auto& f) { return f->HasCharacter(c); });
}

SSIZE_T Sqex::FontCsv::CascadingFont::GetCharacterWidth(char32_t c) const {
	for (const auto& f : m_pImpl->m_fontList)
		if (const auto w = f->GetCharacterWidth(c))
			return w;
	return 0;
}

float Sqex::FontCsv::CascadingFont::Size() const {
	return m_pImpl->m_size;
}

const std::vector<char32_t>& Sqex::FontCsv::CascadingFont::GetAllCharacters() const {
	if (!m_pImpl->m_characterListDiscovered) {
		std::set<char32_t> result;
		for (const auto& f : m_pImpl->m_fontList)
			for (const auto& c : f->GetAllCharacters())
				result.insert(c);
		m_pImpl->m_characterList.insert(m_pImpl->m_characterList.end(), result.begin(), result.end());
		m_pImpl->m_characterListDiscovered = true;
		return m_pImpl->m_characterList;
	}
	return m_pImpl->m_characterList;
}

uint32_t Sqex::FontCsv::CascadingFont::Ascent() const {
	return m_pImpl->m_ascent;
}

uint32_t Sqex::FontCsv::CascadingFont::Descent() const {
	return m_pImpl->m_descent;
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::CascadingFont::GetKerningTable() const {
	if (!m_pImpl->m_kerningDiscovered) {
		std::map<std::pair<char32_t, char32_t>, SSIZE_T> result;
		for (size_t i = 0; i < m_pImpl->m_fontList.size(); ++i) {
			for (const auto& k : m_pImpl->m_fontList[i]->GetKerningTable()) {
				if (!k.second)
					continue;
				const auto owner1 = m_pImpl->GetCharacterOwnerIndex(k.first.first);
				const auto owner2 = m_pImpl->GetCharacterOwnerIndex(k.first.second);

				if (owner1 == i && owner2 == i) {
					// pass
				} else if (k.first.first == u' ' && owner2 == i) {
					// pass
				} else if (k.first.second == u' ' && owner1 == i) {
					// pass
				} else
					continue;

				result.emplace(k);
			}
		}
		m_pImpl->m_kerningMap = std::move(result);
		m_pImpl->m_kerningDiscovered = true;
	}
	return m_pImpl->m_kerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::CascadingFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	for (const auto& f : GetFontList()) {
		const auto currBbox = f->Measure(x, y + Ascent() - f->Ascent(), c);
		if (!currBbox.empty)
			return { false, currBbox.left, currBbox.top, currBbox.right, currBbox.bottom, currBbox.offsetX };
	}
	return { true };
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::CascadingFont::Measure(SSIZE_T x, SSIZE_T y, const std::u32string & s) const {
	if (s.empty())
		return {};

	char32_t lastChar = 0;
	const auto iHeight = static_cast<SSIZE_T>(Height());

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
		} else if (currChar == u'\u200c') {  // unicode non-joiner
			lastChar = 0;
			continue;
		}

		const auto kerning = GetKerning(lastChar, currChar);
		const auto currBbox = Measure(currX + kerning, currY, currChar);
		if (!currBbox.empty) {
			if (currBbox.offsetX > 100000)
				__debugbreak();
			if (result.empty) {
				result = currBbox;
				result.offsetX = result.right + result.offsetX;
			} else {
				result.left = std::min(result.left, currBbox.left);
				result.top = std::min(result.top, currBbox.top);
				result.right = std::max(result.right, currBbox.right);
				result.bottom = std::max(result.bottom, currBbox.bottom);
				result.offsetX = std::max(result.offsetX, currBbox.right + currBbox.offsetX);
			}
			currX = currBbox.right + currBbox.offsetX;
		}
		lastChar = currChar;
	}
	if (result.empty)
		return { true };

	result.offsetX -= result.right;
	return result;
}

const std::vector<std::shared_ptr<Sqex::FontCsv::SeCompatibleFont>>& Sqex::FontCsv::CascadingFont::GetFontList() const {
	return m_pImpl->m_fontList;
}

struct Sqex::FontCsv::GdiFont::Implementation {
	GdiFont& this_;
	const LOGFONTW m_logfont;
	mutable std::mutex m_wrapperMtx;
	mutable std::vector<std::unique_ptr<DeviceContextWrapper>> m_wrappers;
	const TEXTMETRICW m_textMetric;

	std::map<std::pair<char32_t, char32_t>, SSIZE_T> m_kerningMap;
	std::vector<char32_t> m_characterList;

	static TEXTMETRICW GetTextMetricsW(HDC hdc) {
		TEXTMETRICW result;
		::GetTextMetricsW(hdc, &result);
		return result;
	}

	Implementation(GdiFont& this_, const LOGFONTW& logfont)
		: this_(this_)
		, m_logfont(logfont)
		, m_wrappers([&logfont]() { std::vector<std::unique_ptr<DeviceContextWrapper>> w; w.emplace_back(std::make_unique<DeviceContextWrapper>(logfont)); return w; }())
		, m_textMetric(m_wrappers[0]->Metrics) {

		SYSTEM_INFO si;
		GetNativeSystemInfo(&si);
		m_wrappers.resize(si.dwNumberOfProcessors);

		const auto& ctx = m_wrappers[0];
		{
			std::vector<uint8_t> buffer(GetFontUnicodeRanges(ctx->GetDC(), nullptr));
			auto& glyphset = *reinterpret_cast<GLYPHSET*>(&buffer[0]);
			glyphset.cbThis = static_cast<DWORD>(buffer.size());
			if (!GetFontUnicodeRanges(ctx->GetDC(), &glyphset))
				throw std::runtime_error("a");
			for (DWORD i = 0; i < glyphset.cRanges; ++i) {
				for (USHORT j = 0; j < glyphset.ranges[i].cGlyphs; ++j) {
					const auto c = static_cast<char32_t>(glyphset.ranges[i].wcLow + j);
					m_characterList.push_back(c);
				}
			}
		}
		{
			std::vector<KERNINGPAIR> pairs(GetKerningPairsW(ctx->GetDC(), 0, nullptr));
			if (!pairs.empty() && !GetKerningPairsW(ctx->GetDC(), static_cast<DWORD>(pairs.size()), &pairs[0]))
				throw std::runtime_error("a");
			for (const auto& p : pairs) {
				if (p.iKernAmount)
					m_kerningMap.emplace(std::make_pair(p.wFirst, p.wSecond), p.iKernAmount);
			}
		}

	}
};

Sqex::FontCsv::GdiFont::GdiFont(const LOGFONTW & logfont)
	: m_pImpl(std::make_unique<Implementation>(*this, logfont)) {
}

Sqex::FontCsv::GdiFont::~GdiFont() = default;

bool Sqex::FontCsv::GdiFont::HasCharacter(char32_t c) const {
	const auto& chars = GetAllCharacters();
	const auto it = std::lower_bound(chars.begin(), chars.end(), c);
	return it != chars.end() && *it == c;
}

SSIZE_T Sqex::FontCsv::GdiFont::GetCharacterWidth(char32_t c) const {
	if (!HasCharacter(c))
		return 0;
	return AllocateDeviceContext()->GetCharacterWidth(c);
}

float Sqex::FontCsv::GdiFont::Size() const {
	return static_cast<float>(m_pImpl->m_textMetric.tmHeight);
}

const std::vector<char32_t>& Sqex::FontCsv::GdiFont::GetAllCharacters() const {
	return m_pImpl->m_characterList;
}

uint32_t Sqex::FontCsv::GdiFont::Ascent() const {
	return m_pImpl->m_textMetric.tmAscent;
}

uint32_t Sqex::FontCsv::GdiFont::Descent() const {
	return m_pImpl->m_textMetric.tmDescent;
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::GdiFont::GetKerningTable() const {
	return m_pImpl->m_kerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::GdiFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	if (!HasCharacter(c))
		return { true };
	return AllocateDeviceContext()->Measure(x, y, c);
}

Sqex::FontCsv::GdiFont::DeviceContextWrapper::DeviceContextWrapper(const LOGFONTW & logfont)
	: m_hdc(CreateCompatibleDC(nullptr))
	, m_hdcRelease([this]() { DeleteDC(m_hdc); })

	, m_hFont(CreateFontIndirectW(&logfont))
	, m_fontRelease([this]() { DeleteFont(m_hFont); })
	, m_hPrevFont(SelectFont(m_hdc, m_hFont))
	, m_prevFontRevert([this]() { SelectFont(m_hdc, m_hPrevFont); })
	, Metrics([this]() {
		TEXTMETRICW metrics;
		if (!::GetTextMetricsW(m_hdc, &metrics))
			throw std::runtime_error("GetTextMetricsW failed");
		return metrics;
	}()) {
}

Sqex::FontCsv::GdiFont::DeviceContextWrapper::~DeviceContextWrapper() = default;

SSIZE_T Sqex::FontCsv::GdiFont::DeviceContextWrapper::GetCharacterWidth(char32_t c) const {
	ABCFLOAT w;
	GetCharABCWidthsFloatW(m_hdc, c, c, &w);
	const auto tw = w.abcfA + w.abcfB + w.abcfC;
	const auto tw2 = w.abcfA + w.abcfB;
	return static_cast<SSIZE_T>(static_cast<double>(c == ' ' ? tw : tw2));
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::GdiFont::DeviceContextWrapper::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	GLYPHMETRICS gm{};
	static const MAT2 mat{ {0, 1},{0,0},{0,0},{0,1} };

	if (GDI_ERROR == GetGlyphOutlineW(m_hdc, c, GGO_METRICS, &gm, 0, nullptr, &mat))
		return { true };

	ABCFLOAT w;
	GetCharABCWidthsFloatW(m_hdc, c, c, &w);
	return {
		false,
		x + gm.gmptGlyphOrigin.x,
		y + Metrics.tmAscent - gm.gmptGlyphOrigin.y,
		x + gm.gmptGlyphOrigin.x + gm.gmBlackBoxX,
		y + Metrics.tmAscent - gm.gmptGlyphOrigin.y + gm.gmBlackBoxY,
		static_cast<SSIZE_T>(gm.gmCellIncX) - static_cast<SSIZE_T>(gm.gmBlackBoxX) - gm.gmptGlyphOrigin.x,
	};
}

std::pair<const std::vector<uint8_t>*, Sqex::FontCsv::GlyphMeasurement> Sqex::FontCsv::GdiFont::DeviceContextWrapper::Draw(SSIZE_T x, SSIZE_T y, char32_t c) {
	GLYPHMETRICS gm{};
	static const MAT2 mat{ {0, 1},{0,0},{0,0},{0,1} };

	m_readBuffer.resize(GetGlyphOutlineW(m_hdc, c, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &mat));
	if (!m_readBuffer.empty())
		GetGlyphOutlineW(m_hdc, c, GGO_GRAY8_BITMAP, &gm, static_cast<DWORD>(m_readBuffer.size()), &m_readBuffer[0], &mat);;

	return std::make_pair(&m_readBuffer, GlyphMeasurement{
		false,
		x + gm.gmptGlyphOrigin.x,
		y + Metrics.tmAscent - gm.gmptGlyphOrigin.y,
		x + gm.gmptGlyphOrigin.x + gm.gmBlackBoxX,
		y + Metrics.tmAscent - gm.gmptGlyphOrigin.y + gm.gmBlackBoxY,
		static_cast<SSIZE_T>(gm.gmCellIncX) - static_cast<SSIZE_T>(gm.gmBlackBoxX) - gm.gmptGlyphOrigin.x,
		});
}

Sqex::FontCsv::GdiFont::DeviceContextWrapperContext Sqex::FontCsv::GdiFont::AllocateDeviceContext() const {
	{
		const auto lock = std::lock_guard(m_pImpl->m_wrapperMtx);
		for (auto& i : m_pImpl->m_wrappers) {
			if (i)
				return { this, std::move(i) };
		}
	}
	return { this, std::make_unique<DeviceContextWrapper>(m_pImpl->m_logfont) };
}

void Sqex::FontCsv::GdiFont::FreeDeviceContext(std::unique_ptr<DeviceContextWrapper> wrapper) const {
	const auto lock = std::lock_guard(m_pImpl->m_wrapperMtx);
	for (auto& i : m_pImpl->m_wrappers) {
		if (!i) {
			i = std::move(wrapper);
			return;
		}
	}
}
