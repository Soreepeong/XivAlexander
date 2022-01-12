#include "pch.h"
#include "XivAlexanderCommon/Sqex/FontCsv/GdiFont.h"

struct Sqex::FontCsv::GdiFont::Implementation {
	const LOGFONTW LogFont;
	std::mutex WrapperMtx;
	std::vector<std::unique_ptr<DeviceContextWrapper>> Wrappers;
	const TEXTMETRICW TextMetric;
	const std::vector<char32_t> CharacterList;
	const std::map<std::pair<char32_t, char32_t>, SSIZE_T> KerningMap;

	static TEXTMETRICW GetTextMetricsW(HDC hdc) {
		TEXTMETRICW result;
		::GetTextMetricsW(hdc, &result);
		return result;
	}

	Implementation(const GdiFont* owner, const LOGFONTW& logfont)
		: LogFont(logfont)
		, Wrappers([owner, &logfont]() {
			std::vector<std::unique_ptr<DeviceContextWrapper>> w;
			w.emplace_back(std::make_unique<DeviceContextWrapper>(owner, logfont));
			return w;
		}())
		, TextMetric(Wrappers[0]->Metrics)
		, CharacterList([&ctx = *Wrappers[0]]() {
			std::vector<char32_t> result;
			std::vector<uint8_t> buffer(GetFontUnicodeRanges(ctx.GetDC(), nullptr));
			auto& glyphset = *reinterpret_cast<GLYPHSET*>(&buffer[0]);
			glyphset.cbThis = static_cast<DWORD>(buffer.size());
			if (!GetFontUnicodeRanges(ctx.GetDC(), &glyphset))
				throw std::runtime_error("a");
			for (DWORD i = 0; i < glyphset.cRanges; ++i) {
				for (USHORT j = 0; j < glyphset.ranges[i].cGlyphs; ++j) {
					const auto c = static_cast<char32_t>(glyphset.ranges[i].wcLow + j);
					result.push_back(c);
				}
			}
			return result;
		}())
		, KerningMap([&ctx = *Wrappers[0]]() {
			std::map<std::pair<char32_t, char32_t>, SSIZE_T> result;
			std::vector<KERNINGPAIR> pairs(GetKerningPairsW(ctx.GetDC(), 0, nullptr));
			if (!pairs.empty() && !GetKerningPairsW(ctx.GetDC(), static_cast<DWORD>(pairs.size()), &pairs[0]))
				throw std::runtime_error("a");
			for (const auto& p : pairs) {
				if (p.iKernAmount)
					result.emplace(std::make_pair(p.wFirst, p.wSecond), p.iKernAmount);
			}
			return result;
		}()) {

		Wrappers.resize(Win32::GetCoreCount());
	}
};

Sqex::FontCsv::GdiFont::GdiFont(const LOGFONTW& logfont)
	: m_pImpl(std::make_unique<Implementation>(this, logfont)) {
}

Sqex::FontCsv::GdiFont::~GdiFont() = default;

bool Sqex::FontCsv::GdiFont::HasCharacter(char32_t c) const {
	const auto& chars = GetAllCharacters();
	const auto it = std::ranges::lower_bound(chars, c);
	return it != chars.end() && *it == c;
}

float Sqex::FontCsv::GdiFont::Size() const {
	return static_cast<float>(m_pImpl->TextMetric.tmHeight);
}

const std::vector<char32_t>& Sqex::FontCsv::GdiFont::GetAllCharacters() const {
	return m_pImpl->CharacterList;
}

uint32_t Sqex::FontCsv::GdiFont::Ascent() const {
	return m_pImpl->TextMetric.tmAscent;
}

uint32_t Sqex::FontCsv::GdiFont::LineHeight() const {
	return m_pImpl->TextMetric.tmHeight + m_pImpl->TextMetric.tmExternalLeading;
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::GdiFont::GetKerningTable() const {
	return m_pImpl->KerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::GdiFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	if (!HasCharacter(c))
		return {true};
	return AllocateDeviceContext()->Measure(x, y, c);
}

Sqex::FontCsv::GdiFont::DeviceContextWrapper::DeviceContextWrapper(const GdiFont* owner, const LOGFONTW& logfont)
	: m_owner(owner)
	, m_hdc(CreateCompatibleDC(nullptr))
	, m_hdcRelease([this]() { DeleteDC(m_hdc); })
	, m_hFont(CreateFontIndirectW(&logfont))
	, m_fontRelease([this]() { DeleteFont(m_hFont); })
	, m_hPrevFont(SelectFont(m_hdc, m_hFont))
	, m_prevFontRevert([this]() { SelectFont(m_hdc, m_hPrevFont); })
	, Metrics([this]() {
		TEXTMETRICW metrics;
		if (!GetTextMetricsW(m_hdc, &metrics))
			throw std::runtime_error("GetTextMetricsW failed");
		return metrics;
	}()) {
}

Sqex::FontCsv::GdiFont::DeviceContextWrapper::~DeviceContextWrapper() = default;

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::GdiFont::DeviceContextWrapper::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	static constexpr MAT2 IdentityMatrix = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};

	GLYPHMETRICS gm;
	GetGlyphOutlineW(m_hdc, c, GGO_METRICS, &gm, 0, nullptr, &IdentityMatrix);

	return GlyphMeasurement{
		.empty = false,
		.left = gm.gmptGlyphOrigin.x,
		.top = Metrics.tmAscent - gm.gmptGlyphOrigin.y,
		.right = static_cast<SSIZE_T>(static_cast<SSIZE_T>(gm.gmptGlyphOrigin.x) + gm.gmBlackBoxX),
		.bottom = static_cast<SSIZE_T>(static_cast<SSIZE_T>(Metrics.tmAscent) - gm.gmptGlyphOrigin.y + gm.gmBlackBoxY),
		.advanceX = gm.gmCellIncX + m_owner->m_advanceWidthDelta,
	}.Translate(x, y);
}

Sqex::FontCsv::GdiFont::DeviceContextWrapper::DrawResult Sqex::FontCsv::GdiFont::DeviceContextWrapper::Draw(SSIZE_T x, SSIZE_T y, char32_t c) {
	if (!m_hBitmap) {
		SetTextColor(m_hdc, 0xFFFFFF);
		SetBkColor(m_hdc, 0x0);
		m_hBitmap = CreateBitmap(Align<int>(Metrics.tmMaxCharWidth, 4), Metrics.tmHeight, 1, 32, nullptr);
		m_bitmapRelease = [this]() { DeleteBitmap(m_hBitmap); };
		m_hPrevBitmap = SelectBitmap(m_hdc, m_hBitmap);
		m_prevBitmapRevert = [this]() { SelectBitmap(m_hdc, m_hPrevBitmap); };
		m_bmi.bmi.bmiHeader.biSize = sizeof m_bmi.bmi.bmiHeader;
		if (!GetDIBits(m_hdc, m_hBitmap, 0, 0, nullptr, &m_bmi.bmi, DIB_RGB_COLORS))
			throw std::runtime_error("GetDIBits(1)");
		m_readBuffer.resize(m_bmi.bmi.bmiHeader.biSizeImage / 4);
	}

	GlyphMeasurement res = Measure(0, 0, c);
	ExtTextOutW(m_hdc, static_cast<int>(-res.left), static_cast<int>(-res.top), ETO_OPAQUE, nullptr, ToU16(ToU8({c})).c_str(), 1, nullptr);
	if (!GetDIBits(m_hdc, m_hBitmap, 0, m_bmi.bmi.bmiHeader.biHeight, &m_readBuffer[0], &m_bmi.bmi, DIB_RGB_COLORS))
		throw std::runtime_error("GetDIBits(2)");

	return {&m_readBuffer, res.Translate(x, y), m_bmi.bmi.bmiHeader.biWidth, m_bmi.bmi.bmiHeader.biHeight};
}

Sqex::FontCsv::GdiFont::DeviceContextWrapperContext Sqex::FontCsv::GdiFont::AllocateDeviceContext() const {
	{
		const auto lock = std::lock_guard(m_pImpl->WrapperMtx);
		for (auto& i : m_pImpl->Wrappers) {
			if (i)
				return {this, std::move(i)};
		}
	}
	return {this, std::make_unique<DeviceContextWrapper>(this, m_pImpl->LogFont)};
}

void Sqex::FontCsv::GdiFont::FreeDeviceContext(std::unique_ptr<DeviceContextWrapper> wrapper) const {
	const auto lock = std::lock_guard(m_pImpl->WrapperMtx);
	for (auto& i : m_pImpl->Wrappers) {
		if (!i) {
			i = std::move(wrapper);
			return;
		}
	}
}
