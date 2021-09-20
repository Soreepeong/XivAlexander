#include "pch.h"
#include "Sqex_FontCsv_DirectWriteFont.h"

#include "Sqex_FontCsv_FreeTypeFont.h"

#pragma comment(lib, "dwrite.lib")

_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory3, __uuidof(IDWriteFactory3));
_COM_SMARTPTR_TYPEDEF(IDWriteFont, __uuidof(IDWriteFont));
_COM_SMARTPTR_TYPEDEF(IDWriteFontCollection, __uuidof(IDWriteFontCollection));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace, __uuidof(IDWriteFontFace));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace1, __uuidof(IDWriteFontFace1));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFace3, __uuidof(IDWriteFontFace3));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFaceReference, __uuidof(IDWriteFontFaceReference));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFamily, __uuidof(IDWriteFontFamily));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFile, __uuidof(IDWriteFontFile));
_COM_SMARTPTR_TYPEDEF(IDWriteFontFileLoader, __uuidof(IDWriteFontFileLoader));
_COM_SMARTPTR_TYPEDEF(IDWriteFontSetBuilder, __uuidof(IDWriteFontSetBuilder));
_COM_SMARTPTR_TYPEDEF(IDWriteGdiInterop, __uuidof(IDWriteGdiInterop));
_COM_SMARTPTR_TYPEDEF(IDWriteGlyphRunAnalysis, __uuidof(IDWriteGlyphRunAnalysis));
_COM_SMARTPTR_TYPEDEF(IDWriteLocalFontFileLoader, __uuidof(IDWriteLocalFontFileLoader));

struct Sqex::FontCsv::DirectWriteFont::Implementation {
	const float Size;
	const IDWriteFactory3Ptr Factory;
	const IDWriteFontFace1Ptr Face1;
	const IDWriteFontFace3Ptr Face3;
	const DWRITE_FONT_METRICS1 Metrics;
	const DWRITE_RENDERING_MODE RenderMode;

	std::unique_ptr<FreeTypeFont> FreeTypeFont;

	std::mutex DiscoveryMtx;

	bool KerningDiscovered = false;
	std::map<std::pair<char32_t, char32_t>, SSIZE_T> KerningMap;

	bool CharacterListDiscovered = false;
	std::vector<char32_t> CharacterList;
	std::map<uint16_t, char32_t> GlyphIndexToCharCodeMap;

	std::mutex BuffersMtx;
	std::vector<std::unique_ptr<std::vector<uint8_t>>> Buffers;

	void LoadCharacterList() {
		if (CharacterListDiscovered)
			return;

		uint32_t rangeCount;
		if (const auto hr = Face1->GetUnicodeRanges(0, nullptr, &rangeCount);
			hr != E_NOT_SUFFICIENT_BUFFER && hr != S_OK)
			Succ(hr);
		std::vector<DWRITE_UNICODE_RANGE> ranges(rangeCount);
		Succ(Face1->GetUnicodeRanges(rangeCount, &ranges[0], &rangeCount));

		for (const auto& range : ranges)
			for (uint32_t i = range.first; i <= range.last; ++i)
				CharacterList.push_back(i);

		std::vector<uint16_t> indices(CharacterList.size());
		Succ(Face1->GetGlyphIndicesW(reinterpret_cast<UINT32 const*>(CharacterList.data()), static_cast<uint32_t>(CharacterList.size()), &indices[0]));
		for (size_t i = 0; i < indices.size(); ++i)
			GlyphIndexToCharCodeMap.emplace(indices[i], CharacterList[i]);

		CharacterListDiscovered = true;
	}

	template<typename From, typename To = SSIZE_T, typename = std::enable_if_t<std::is_arithmetic_v<From> && std::is_arithmetic_v<To>>>
	To ConvVal(From f) {
		return static_cast<To>(std::floor(static_cast<double>(f) * static_cast<double>(Size) / static_cast<double>(Metrics.designUnitsPerEm)));
	}

	[[nodiscard]] GlyphMeasurement ScaleMeasurement(GlyphMeasurement gm) const {
		return gm.Scale(Size, Metrics.designUnitsPerEm);
	}
};

Sqex::FontCsv::DirectWriteFont::DirectWriteFont(const wchar_t* fontName, float size, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, DWRITE_RENDERING_MODE renderMode)
	: m_pImpl([fontName, size, weight, stretch, style, renderMode]() {
		IDWriteFactory3Ptr factory;
		Succ(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(&factory)));

		IDWriteFontCollectionPtr coll;
		Succ(factory->GetSystemFontCollection(&coll));

		uint32_t index;
		BOOL exists;
		Succ(coll->FindFamilyName(fontName, &index, &exists));
		if (!exists)
			throw std::invalid_argument("Font not found");

		IDWriteFontFamilyPtr family;
		Succ(coll->GetFontFamily(index, &family));

		IDWriteFontPtr font;
		Succ(family->GetFirstMatchingFont(weight, stretch, style, &font));

		IDWriteFontFacePtr fontFace;
		Succ(font->CreateFontFace(&fontFace));

		IDWriteFontFace1Ptr fontFace1;
		Succ(fontFace.QueryInterface(decltype(fontFace1)::GetIID(), &fontFace1));

		DWRITE_FONT_METRICS1 metrics{};
		fontFace1->GetMetrics(&metrics);

		// Face3 is optional
		IDWriteFontFace3Ptr fontFace3;
		fontFace.QueryInterface(decltype(fontFace3)::GetIID(), &fontFace3);

		return std::make_unique<Implementation>(
			size,
			factory,
			fontFace1,
			fontFace3,
			metrics,
			renderMode
		);
	}()) {
}

Sqex::FontCsv::DirectWriteFont::DirectWriteFont(const std::filesystem::path& path, uint32_t faceIndex, float size, DWRITE_RENDERING_MODE renderMode)
	: m_pImpl([&path, faceIndex, size, renderMode]() {
		IDWriteFactory3Ptr factory;
		Succ(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(&factory)));

		IDWriteFontSetBuilderPtr builder;
		Succ(factory->CreateFontSetBuilder(&builder));

		IDWriteFontFilePtr fontFile;
		Succ(factory->CreateFontFileReference(path.wstring().c_str(), nullptr, &fontFile));

		BOOL isSupported;
		DWRITE_FONT_FILE_TYPE fileType;
		UINT32 numberOfFonts;
		Succ(fontFile->Analyze(&isSupported, &fileType, nullptr, &numberOfFonts));

		if (!isSupported)
			throw std::invalid_argument("Font type not supported");

		IDWriteFontFaceReferencePtr fontFaceReference;
		Succ(factory->CreateFontFaceReference(fontFile, faceIndex, DWRITE_FONT_SIMULATIONS_NONE, &fontFaceReference));

		IDWriteFontFace3Ptr fontFace3;
		Succ(fontFaceReference->CreateFontFace(&fontFace3));

		IDWriteFontFace1Ptr fontFace1;
		Succ(fontFace3.QueryInterface(decltype(fontFace1)::GetIID(), &fontFace1));

		DWRITE_FONT_METRICS1 metrics{};
		fontFace1->GetMetrics(&metrics);

		return std::make_unique<Implementation>(
			size,
			factory,
			fontFace1,
			fontFace3,
			metrics,
			renderMode
		);
	}()) {
}

Sqex::FontCsv::DirectWriteFont::~DirectWriteFont() = default;

bool Sqex::FontCsv::DirectWriteFont::HasCharacter(char32_t c) const {
	const auto& all = GetAllCharacters();
	return std::ranges::find(all, c) != all.end();
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
	constexpr auto a = static_cast<SSIZE_T>(0);
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
	return static_cast<uint32_t>(std::ceil(m_pImpl->Size * static_cast<float>(m_pImpl->Metrics.ascent) / static_cast<float>(m_pImpl->Metrics.designUnitsPerEm)));
}

uint32_t Sqex::FontCsv::DirectWriteFont::LineHeight() const {
	return static_cast<uint32_t>(std::ceil(m_pImpl->Size * static_cast<float>(m_pImpl->Metrics.ascent + m_pImpl->Metrics.descent + m_pImpl->Metrics.lineGap) / static_cast<float>(m_pImpl->Metrics.designUnitsPerEm)));
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::DirectWriteFont::GetKerningTable() const {
	if (!m_pImpl->KerningDiscovered) {
		const auto lock = std::lock_guard(m_pImpl->DiscoveryMtx);
		if (!m_pImpl->KerningDiscovered) {
			if (m_pImpl->Face1->HasKerningPairs()) {
				const char* data;
				void* context;
				uint32_t size;
				BOOL exists;
				Succ(m_pImpl->Face1->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('k', 'e', 'r', 'n'), reinterpret_cast<const void**>(&data), &size, &context, &exists));
				if (exists) {
					m_pImpl->LoadCharacterList();
					m_pImpl->KerningMap = ParseKerningTable(std::span(data, size), m_pImpl->GlyphIndexToCharCodeMap);
					m_pImpl->Face1->ReleaseFontTable(context);
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

void Sqex::FontCsv::DirectWriteFont::SetMeasureWithFreeType() {
	auto [path, faceIndex] = GetFontFile();
	m_pImpl->FreeTypeFont = std::make_unique<FreeTypeFont>(std::move(path), faceIndex, m_pImpl->Size);
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::DirectWriteFont::DrawCharacter(char32_t c, std::vector<uint8_t>& buf, bool draw) const {
	uint16_t glyphIndex;
	Succ(m_pImpl->Face1->GetGlyphIndicesW(reinterpret_cast<UINT32 const*>(&c), 1, &glyphIndex));
	if (!glyphIndex)
		return {true};

	DWRITE_GLYPH_METRICS glyphMetrics{};
	Succ(m_pImpl->Face1->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics));

	float glyphAdvance = 0;
	DWRITE_GLYPH_OFFSET glyphOffset{};
	const auto run = DWRITE_GLYPH_RUN{
		.fontFace = m_pImpl->Face1,
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
	if (m_pImpl->FreeTypeFont)
		bbox.advanceX = m_pImpl->FreeTypeFont->Measure(0, 0, c).advanceX;
	else
		bbox.advanceX = m_pImpl->ConvVal<UINT32, SSIZE_T>(glyphMetrics.advanceWidth);
	bbox.advanceX += m_advanceWidthDelta;
	if (!bbox.EffectivelyEmpty() && draw) {
		buf.resize(bbox.Area());
		Succ(analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, bbox.AsMutableRectPtr(), &buf[0], static_cast<uint32_t>(buf.size())));
	}

	return bbox;
}

std::tuple<std::filesystem::path, int> Sqex::FontCsv::DirectWriteFont::GetFontFile() const {
	if (!m_pImpl->Face3)
		throw std::runtime_error("Unsupported on this version of Windows");

	IDWriteFontFaceReferencePtr ref;
	Succ(m_pImpl->Face3->GetFontFaceReference(&ref));

	IDWriteFontFilePtr file;
	Succ(ref->GetFontFile(&file));

	IDWriteFontFileLoaderPtr loader;
	Succ(file->GetLoader(&loader));

	void const* refKey;
	UINT32 refKeySize;
	Succ(file->GetReferenceKey(&refKey, &refKeySize));

	IDWriteLocalFontFileLoaderPtr localFileLoader;
	Succ(loader.QueryInterface(decltype(localFileLoader)::GetIID(), &localFileLoader));

	UINT32 bufLen;
	Succ(localFileLoader->GetFilePathLengthFromKey(refKey, refKeySize, &bufLen));

	std::wstring buf(bufLen + 1, L'\0');
	Succ(localFileLoader->GetFilePathFromKey(refKey, refKeySize, &buf[0], bufLen + 1));
	buf.resize(bufLen);

	return {buf, ref->GetFontFaceIndex()};
}

Sqex::FontCsv::DirectWriteFont::DwriteRenderBufferCtxMgr::~DwriteRenderBufferCtxMgr() {
	if (m_wrapper) {
		const auto lock = std::lock_guard(m_pImpl->BuffersMtx);
		for (auto& i : m_pImpl->Buffers) {
			if (!i) {
				i = std::move(m_wrapper);
				return;
			}
		}
	}
}

Sqex::FontCsv::DirectWriteFont::DwriteRenderBufferCtxMgr Sqex::FontCsv::DirectWriteFont::AllocateBuffer() const {
	{
		const auto lock = std::lock_guard(m_pImpl->BuffersMtx);
		for (auto& i : m_pImpl->Buffers) {
			if (i)
				return DwriteRenderBufferCtxMgr(m_pImpl.get(), std::move(i));
		}
	}
	return DwriteRenderBufferCtxMgr(m_pImpl.get(), std::make_unique<std::vector<uint8_t>>());
}
