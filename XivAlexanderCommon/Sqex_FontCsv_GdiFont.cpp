#include "pch.h"
#include "Sqex_FontCsv_GdiFont.h"

struct Sqex::FontCsv::GdiFont::Implementation {
	GdiFont& this_;
	const LOGFONTW m_logfont;
	std::mutex m_wrapperMtx;
	std::vector<std::unique_ptr<DeviceContextWrapper>> m_wrappers;
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

Sqex::FontCsv::GdiFont::GdiFont(const LOGFONTW& logfont)
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
	
	auto res = GlyphMeasurement{
		false,
		0,
		0,
		static_cast<SSIZE_T>(gm.gmBlackBoxX),
		static_cast<SSIZE_T>(gm.gmBlackBoxY),
	};
	res.advanceX = static_cast<SSIZE_T>(gm.gmCellIncX);
	res.Translate(x + gm.gmptGlyphOrigin.x, y + Metrics.tmAscent - gm.gmptGlyphOrigin.y);
	return res;
}

std::pair<const std::vector<uint8_t>*, Sqex::FontCsv::GlyphMeasurement> Sqex::FontCsv::GdiFont::DeviceContextWrapper::Draw(SSIZE_T x, SSIZE_T y, char32_t c) {
	GLYPHMETRICS gm{};
	static const MAT2 mat{ {0, 1},{0,0},{0,0},{0,1} };

	m_readBuffer.resize(GetGlyphOutlineW(m_hdc, c, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &mat));
	if (!m_readBuffer.empty())
		GetGlyphOutlineW(m_hdc, c, GGO_GRAY8_BITMAP, &gm, static_cast<DWORD>(m_readBuffer.size()), &m_readBuffer[0], &mat);;
	
	auto res = GlyphMeasurement{
		false,
		0,
		0,
		static_cast<SSIZE_T>(gm.gmBlackBoxX),
		static_cast<SSIZE_T>(gm.gmBlackBoxY),
	};
	res.advanceX = static_cast<SSIZE_T>(gm.gmCellIncX);
	res.Translate(x + gm.gmptGlyphOrigin.x, y + Metrics.tmAscent - gm.gmptGlyphOrigin.y);
	return std::make_pair(&m_readBuffer, res);
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
