#include "pch.h"
#include "Sqex_FontCsv_DirectWriteFont.h"

#pragma comment(lib, "dwrite.lib")

inline void Succ(HRESULT hr) {
	if (!SUCCEEDED(hr)) {
		const auto err = _com_error(hr);
		throw std::runtime_error(std::format("Error 0x{:08x}: {}",
			static_cast<uint32_t>(err.Error()),
			err.ErrorMessage()
		));
	}
}

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory3, __uuidof(IDWriteFactory3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily, __uuidof(IDWriteFontFamily));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace, __uuidof(IDWriteFontFace));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace1, __uuidof(IDWriteFontFace1));
_COM_SMARTPTR_TYPEDEF(IDWriteGdiInterop, __uuidof(IDWriteGdiInterop));
_COM_SMARTPTR_TYPEDEF(IDWriteGlyphRunAnalysis, __uuidof(IDWriteGlyphRunAnalysis));

struct Sqex::FontCsv::DirectWriteFont::Implementation {
	float Size;
	IDWriteFactory3Ptr Factory;
	IDWriteFontPtr Font;
	IDWriteFontFace1Ptr Face;
	DWRITE_FONT_METRICS1 Metrics;

	std::mutex DiscoveryMtx;

	bool KerningDiscovered = false;
	std::map<std::pair<char32_t, char32_t>, SSIZE_T> KerningMap;

	bool CharacterListDiscovered = false;
	std::vector<char32_t> CharacterList;
	std::map<uint16_t, char32_t> GlyphIndexToCharCodeMap;

	std::mutex BuffersMtx;
	std::vector<std::unique_ptr<std::vector<uint8_t>>> Buffers;
	DWRITE_RENDERING_MODE RenderMode;

	void LoadCharacterList() {
		if (CharacterListDiscovered)
			return;

		uint32_t rangeCount;
		if (const auto hr = Face->GetUnicodeRanges(0, nullptr, &rangeCount);
			hr != E_NOT_SUFFICIENT_BUFFER && hr != S_OK)
			Succ(hr);
		std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
		Succ(Face->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

		for (const auto& range : ranges)
			for (uint32_t i = range.first; i <= range.last; ++i)
				CharacterList.push_back(i);

		std::vector<uint16_t> indices(CharacterList.size());
		Succ(Face->GetGlyphIndicesW(reinterpret_cast<UINT32 const*>(CharacterList.data()), static_cast<uint32_t>(CharacterList.size()), &indices[0]));
		for (size_t i = 0; i < indices.size(); ++i)
			GlyphIndexToCharCodeMap.emplace(indices[i], CharacterList[i]);

		CharacterListDiscovered = true;
	}

	struct KernFormat0 {
		BE<uint16_t> nPairs;
		BE<uint16_t> searchRange;
		BE<uint16_t> entrySelector;
		BE<uint16_t> rangeShift;
	};
	struct KernFormat0Pair {
		BE<uint16_t> left;
		BE<uint16_t> right;
		BE<int16_t> value;
	};

	void ParseKerning_Apple(std::span<const char> data) {
		// https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6kern.html

		// TODO: see if this works lol

		struct KernHeaderV1 {
			BE<uint32_t> version;
			BE<uint32_t> nTables;
		};
		struct KernSubtableHeaderV1 {
			BE<uint32_t> length;
			struct CoverageV1 {
				BE<uint8_t> format;
				uint8_t vertical : 1;
				uint8_t crossStream : 1;
				uint8_t variation : 1;
				uint8_t reserved1 : 5;
			} coverage;
		};

		if (data.size_bytes() < sizeof KernHeaderV1)
			return;  // invalid kern table

		const auto& kernHeader = *reinterpret_cast<const KernHeaderV1*>(data.data());
		if (kernHeader.version == 0x10000 && data.size_bytes() >= 8) {
			LoadCharacterList();

			data = data.subspan(sizeof kernHeader);
			for (size_t i = 0; i < kernHeader.nTables; ++i) {
				if (data.size_bytes() < sizeof KernSubtableHeaderV1)
					return;  // invalid kern table

				const auto& kernSubtableHeader = *reinterpret_cast<const KernSubtableHeaderV1*>(data.data());
				if (data.size_bytes() < kernSubtableHeader.length)
					return;  // invalid kern table

				if (!kernSubtableHeader.coverage.vertical
					&& kernSubtableHeader.coverage.format == 0) {

					auto format0Ptr = data.subspan(sizeof kernSubtableHeader);
					if (format0Ptr.size_bytes() < sizeof KernFormat0)
						return;  // invalid kern table

					const auto& kernFormat0 = *reinterpret_cast<const KernFormat0*>(format0Ptr.data());
					format0Ptr = format0Ptr.subspan(sizeof kernFormat0);

					const auto pairs = std::span(reinterpret_cast<const KernFormat0Pair*>(format0Ptr.data()), kernFormat0.nPairs);
					if (pairs.size_bytes() > format0Ptr.size_bytes())
						return;  // invalid kern table

					for (const auto& pair : pairs) {
						const auto advPx = static_cast<SSIZE_T>(pair.value * Size / Metrics.designUnitsPerEm);
						if (!advPx)
							continue;

						const auto key = std::make_pair(GlyphIndexToCharCodeMap[pair.left], GlyphIndexToCharCodeMap[pair.right]);
						KerningMap[key] += advPx;
					}

					data = data.subspan(kernSubtableHeader.length);
				} else
					data = data.subspan(kernSubtableHeader.length);
			}
		}
	}

	void ParseKerning(std::span<const char> data) {
		// https://docs.microsoft.com/en-us/typography/opentype/spec/kern
		struct KernHeaderV0 {
			BE<uint16_t> version;
			BE<uint16_t> nTables;
		};
		struct KernSubtableHeaderV0 {
			BE<uint16_t> version;
			BE<uint16_t> length;
			struct CoverageV0 {
				BE<uint8_t> format;
				uint8_t horizontal : 1;
				uint8_t minimum : 1;
				uint8_t crossStream : 1;
				uint8_t override : 1;
				uint8_t reserved1 : 4;
			} coverage;
		};

		if (data.size_bytes() < sizeof KernHeaderV0)
			return;  // invalid kern table

		const auto& kernHeader = *reinterpret_cast<const KernHeaderV0*>(data.data());
		if (kernHeader.version == 1 && data.size_bytes() >= 4) {
			ParseKerning_Apple(data);

		} else if (kernHeader.version == 0) {
			LoadCharacterList();

			data = data.subspan(sizeof kernHeader);
			for (size_t i = 0; i < kernHeader.nTables; ++i) {
				if (data.size_bytes() < sizeof KernSubtableHeaderV0)
					return;  // invalid kern table

				const auto& kernSubtableHeader = *reinterpret_cast<const KernSubtableHeaderV0*>(data.data());
				if (data.size_bytes() < kernSubtableHeader.length)
					return;  // invalid kern table

				if (kernSubtableHeader.version == 0
					&& kernSubtableHeader.coverage.horizontal
					&& kernSubtableHeader.coverage.format == 0) {

					auto format0Ptr = data.subspan(sizeof kernSubtableHeader);
					if (format0Ptr.size_bytes() < sizeof KernFormat0)
						return;  // invalid kern table

					const auto& kernFormat0 = *reinterpret_cast<const KernFormat0*>(format0Ptr.data());
					format0Ptr = format0Ptr.subspan(sizeof kernFormat0);

					const auto pairs = std::span(reinterpret_cast<const KernFormat0Pair*>(format0Ptr.data()), kernFormat0.nPairs);
					if (pairs.size_bytes() > format0Ptr.size_bytes())
						return;  // invalid kern table

					for (const auto& pair : pairs) {
						const auto advPx = static_cast<SSIZE_T>(pair.value * Size / Metrics.designUnitsPerEm);
						if (!advPx)
							continue;

						const auto key = std::make_pair(GlyphIndexToCharCodeMap[pair.left], GlyphIndexToCharCodeMap[pair.right]);
						if (kernSubtableHeader.coverage.override)
							KerningMap[key] = advPx;
						else
							KerningMap[key] += advPx;
					}

					data = data.subspan(kernSubtableHeader.length);
				} else
					data = data.subspan(kernSubtableHeader.length);
			}
		}
	}

	template<typename From, typename To = SSIZE_T, typename = std::enable_if_t<std::is_arithmetic_v<From>&& std::is_arithmetic_v<To>>>
	To ConvVal(From f) {
		return static_cast<To>(std::floor(static_cast<double>(f) * static_cast<double>(Size) / static_cast<double>(Metrics.designUnitsPerEm)));
	}

	GlyphMeasurement ScaleMeasurement(GlyphMeasurement gm) {
		return gm.Scale(Size, Metrics.designUnitsPerEm);
	}
};

Sqex::FontCsv::DirectWriteFont::DirectWriteFont(const wchar_t* fontName, float size, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, DWRITE_RENDERING_MODE renderMode)
	: m_pImpl(std::make_unique<Implementation>()) {
	m_pImpl->Size = size;
	m_pImpl->RenderMode = renderMode;

	Succ(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(&m_pImpl->Factory)));

	IDWriteFontCollectionPtr coll;
	Succ(m_pImpl->Factory->GetSystemFontCollection(&coll));

	uint32_t index;
	BOOL exists;
	Succ(coll->FindFamilyName(fontName, &index, &exists));
	if (!exists)
		throw std::invalid_argument("Font not found");

	IDWriteFontFamilyPtr family;
	Succ(coll->GetFontFamily(index, &family));

	Succ(family->GetFirstMatchingFont(weight, stretch, style, &m_pImpl->Font));

	IDWriteFontFacePtr face;
	Succ(m_pImpl->Font->CreateFontFace(&face));
	Succ(face.QueryInterface(decltype(m_pImpl->Face)::GetIID(), &m_pImpl->Face));
	m_pImpl->Face->GetMetrics(&m_pImpl->Metrics);
}

Sqex::FontCsv::DirectWriteFont::~DirectWriteFont() = default;

bool Sqex::FontCsv::DirectWriteFont::HasCharacter(char32_t c) const {
	BOOL exists;
	Succ(m_pImpl->Font->HasCharacter(c, &exists));
	return exists;
}

SSIZE_T Sqex::FontCsv::DirectWriteFont::GetCharacterWidth(char32_t c) const {
	uint16_t glyphIndex;
	Succ(m_pImpl->Face->GetGlyphIndicesW(reinterpret_cast<UINT32 const*>(&c), 1, &glyphIndex));
	if (!glyphIndex)
		return 0;

	DWRITE_GLYPH_METRICS glyphMetrics;
	Succ(m_pImpl->Face->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics));

	return (glyphMetrics.leftSideBearing + glyphMetrics.advanceWidth) / m_pImpl->Metrics.designUnitsPerEm;
}

float Sqex::FontCsv::DirectWriteFont::Size() const {
	return m_pImpl->Size;
}

const std::vector<char32_t>& Sqex::FontCsv::DirectWriteFont::GetAllCharacters() const {
	if (!m_pImpl->CharacterListDiscovered) {
		const auto lock = std::lock_guard(m_pImpl->DiscoveryMtx);
		m_pImpl->LoadCharacterList();
	}
	return m_pImpl->CharacterList;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::DirectWriteFont::MaxBoundingBox() const {
	const auto a = static_cast<SSIZE_T>(0);
	return GlyphMeasurement{
		false,
		m_pImpl->Metrics.glyphBoxLeft,
		a + m_pImpl->Metrics.ascent + m_pImpl->Metrics.glyphBoxBottom, // sic
		m_pImpl->Metrics.glyphBoxRight,
		a + m_pImpl->Metrics.ascent + m_pImpl->Metrics.glyphBoxTop, // sic
		0,
	}.Scale(m_pImpl->Size, m_pImpl->Metrics.designUnitsPerEm);
}

uint32_t Sqex::FontCsv::DirectWriteFont::Ascent() const {
	return static_cast<uint32_t>(std::round(m_pImpl->Size * static_cast<float>(m_pImpl->Metrics.ascent) / static_cast<float>(m_pImpl->Metrics.designUnitsPerEm)));
}

uint32_t Sqex::FontCsv::DirectWriteFont::Descent() const {
	return static_cast<uint32_t>(std::round(m_pImpl->Size * static_cast<float>(m_pImpl->Metrics.descent) / static_cast<float>(m_pImpl->Metrics.designUnitsPerEm)));
}

uint32_t Sqex::FontCsv::DirectWriteFont::Height() const {
	return SeCompatibleFont::Height();
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::DirectWriteFont::GetKerningTable() const {
	if (!m_pImpl->KerningDiscovered) {
		const auto lock = std::lock_guard(m_pImpl->DiscoveryMtx);
		if (!m_pImpl->KerningDiscovered) {
			if (m_pImpl->Face->HasKerningPairs()) {
				const char* data;
				void* context;
				uint32_t size;
				BOOL exists;
				Succ(m_pImpl->Face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('k', 'e', 'r', 'n'), reinterpret_cast<const void**>(&data), &size, &context, &exists));
				if (exists) {
					m_pImpl->ParseKerning(std::span(data, size));
					m_pImpl->Face->ReleaseFontTable(context);
				}
			}
			m_pImpl->KerningDiscovered = true;
		}
	}
	return m_pImpl->KerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::DirectWriteFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	std::vector<uint8_t> u;
	return DrawCharacter(c, u, false).Translate(x, y);
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::DirectWriteFont::DrawCharacter(char32_t c, std::vector<uint8_t>&buf, bool draw) const {
	uint16_t glyphIndex;
	Succ(m_pImpl->Face->GetGlyphIndicesW(reinterpret_cast<UINT32 const*>(&c), 1, &glyphIndex));
	if (!glyphIndex)
		return { true };

	DWRITE_GLYPH_METRICS glyphMetrics;
	Succ(m_pImpl->Face->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics));

	float glyphAdvance = 0;
	DWRITE_GLYPH_OFFSET glyphOffset{};
	const auto run = DWRITE_GLYPH_RUN{
		.fontFace = m_pImpl->Face,
		.fontEmSize = m_pImpl->Size,
		.glyphCount = 1,
		.glyphIndices = &glyphIndex,
		.glyphAdvances = &glyphAdvance,
		.glyphOffsets = &glyphOffset,
		.isSideways = FALSE,
		.bidiLevel = 0,
	};

	IDWriteGlyphRunAnalysisPtr analysis;
	Succ(m_pImpl->Factory->CreateGlyphRunAnalysis(
		&run,
		nullptr,
		m_pImpl->RenderMode,
		DWRITE_MEASURING_MODE_GDI_NATURAL,
		DWRITE_GRID_FIT_MODE_ENABLED,
		DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
		0,
		static_cast<float>(m_pImpl->ConvVal(m_pImpl->Metrics.ascent)),
		&analysis));

	GlyphMeasurement bbox;
	Succ(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, bbox.AsMutableRectPtr()));
	if (bbox.EffectivelyEmpty()) {
		constexpr SSIZE_T a = 0;
		return m_pImpl->ScaleMeasurement({
			false,
			a,
			a,
			a + glyphMetrics.advanceWidth,
			a + Height(),
			glyphMetrics.advanceWidth,
			});
	}
	
	bbox.advanceX = m_pImpl->ConvVal(glyphMetrics.advanceWidth);

	if (draw) {
		buf.resize(bbox.Area());
		Succ(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, bbox.AsMutableRectPtr(), &buf[0], static_cast<uint32_t>(buf.size())));
	}

	return bbox;
}

Sqex::FontCsv::DirectWriteFont::BufferContext Sqex::FontCsv::DirectWriteFont::AllocateBuffer() const {
	{
		const auto lock = std::lock_guard(m_pImpl->BuffersMtx);
		for (auto& i : m_pImpl->Buffers) {
			if (i)
				return { this, std::move(i) };
		}
	}
	return { this, std::make_unique<std::vector<uint8_t>>() };
}

void Sqex::FontCsv::DirectWriteFont::FreeBuffer(std::unique_ptr<std::vector<uint8_t>> wrapper) const {
	const auto lock = std::lock_guard(m_pImpl->BuffersMtx);
	for (auto& i : m_pImpl->Buffers) {
		if (!i) {
			i = std::move(wrapper);
			return;
		}
	}
}
