#include "pch.h"
#include "Sqex_FontCsv_FreeTypeFont.h"

#include "Sqex_FontCsv_DirectWriteFont.h"

struct Sqex::FontCsv::FreeTypeFont::Implementation {
	const std::filesystem::path Path;
	const int FaceIndex;
	const float Size;
	const size_t CoreCount;
	std::recursive_mutex LibraryMtx;
	FT_Library Library;
	std::vector<FT_Face> FaceSlots;

	bool KerningDiscovered = false;
	std::map<std::pair<char32_t, char32_t>, SSIZE_T> KerningMap;

	bool CharacterListDiscovered = false;
	std::vector<char32_t> CharacterList;
	std::map<uint16_t, char32_t> GlyphIndexToCharCodeMap;

	CallOnDestruction::Multiple Cleanup;

	Implementation(std::filesystem::path path, int faceIndex, float size)
		: Path(std::move(path))
		, FaceIndex(faceIndex)
		, Size(size)
		, CoreCount([]() { SYSTEM_INFO si; GetSystemInfo(&si); return si.dwNumberOfProcessors; }()) {
		FTSucc(FT_Init_FreeType(&Library));
		Cleanup += [this]() { FTSucc(FT_Done_FreeType(Library)); };
	}

	~Implementation() {
		Cleanup.Clear();
	}
};

Sqex::FontCsv::FreeTypeFont::FreeTypeFont(std::filesystem::path path, int faceIndex, float size, FT_Int32 loadFlags)
	: m_loadFlags(loadFlags)
	, m_pImpl(std::make_unique<Implementation>(std::move(path), faceIndex, size)) {
}

Sqex::FontCsv::FreeTypeFont::FreeTypeFont(const wchar_t* fontName, float size, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, FT_Int32 loadFlags)
	: m_loadFlags(loadFlags)
	, m_pImpl([&]() {
	auto [path, faceIndex] = DirectWriteFont(fontName, size, weight, stretch, style).GetFontFile();
	return std::make_unique<Implementation>(std::move(path), faceIndex, size);
}()) {
}

Sqex::FontCsv::FreeTypeFont::~FreeTypeFont() {
	try {
		for (const auto& slot : m_pImpl->FaceSlots) {
			if (!slot)
				throw std::runtime_error("All face must be returned by destruction");
			FTSucc(FT_Done_Face(slot));
		}
	} catch (...) {
		// empty
	}
}

bool Sqex::FontCsv::FreeTypeFont::HasCharacter(char32_t c) const {
	return FT_Get_Char_Index(*GetFace(), c);
}

SSIZE_T Sqex::FontCsv::FreeTypeFont::GetCharacterWidth(char32_t c) const {
	auto face = GetFace();
	FTSucc(FT_Load_Char(*face, c, m_loadFlags));
	return face->glyph->bitmap.width;
}

float Sqex::FontCsv::FreeTypeFont::Size() const {
	return m_pImpl->Size;
}

const std::vector<char32_t>& Sqex::FontCsv::FreeTypeFont::GetAllCharacters() const {
	if (m_pImpl->CharacterListDiscovered)
		return m_pImpl->CharacterList;

	const auto lock = std::lock_guard(m_pImpl->LibraryMtx);
	if (m_pImpl->CharacterListDiscovered)
		return m_pImpl->CharacterList;

	auto face = GetFace();
	std::vector<char32_t> result;
	FT_UInt glyphIndex;
	for (char32_t c = FT_Get_First_Char(*face, &glyphIndex); glyphIndex; c = FT_Get_Next_Char(*face, c, &glyphIndex)) {
		result.push_back(c);
		m_pImpl->GlyphIndexToCharCodeMap.emplace(glyphIndex, c);
	}

	// order of execution matters
	m_pImpl->CharacterList = std::move(result);
	m_pImpl->CharacterListDiscovered = true;
	return m_pImpl->CharacterList;
}

uint32_t Sqex::FontCsv::FreeTypeFont::Ascent() const {
	return GetFace()->size->metrics.ascender / 64;
}

uint32_t Sqex::FontCsv::FreeTypeFont::Descent() const {
	return GetFace()->size->metrics.descender / -64;
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::FreeTypeFont::GetKerningTable() const {
	if (m_pImpl->KerningDiscovered)
		return m_pImpl->KerningMap;

	const auto lock = std::lock_guard(m_pImpl->LibraryMtx);
	if (m_pImpl->KerningDiscovered)
		return m_pImpl->KerningMap;

	// order of execution matters
	m_pImpl->KerningMap = DirectWriteFont(m_pImpl->Path, m_pImpl->FaceIndex, m_pImpl->Size).GetKerningTable();
	m_pImpl->KerningDiscovered = true;
	return m_pImpl->KerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::FreeTypeFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	auto face = GetFace();
	FTSucc(FT_Load_Char(*face, c, m_loadFlags));

	if (face->glyph->glyph_index == 0)
		return { true };

	return GlyphMeasurement{
		false,
		face->glyph->bitmap_left,
		face->size->metrics.ascender / 64 - face->glyph->bitmap_top,
		static_cast<SSIZE_T>(0) + face->glyph->bitmap_left + face->glyph->bitmap.width,
		static_cast<SSIZE_T>(0) + face->size->metrics.ascender / 64 - face->glyph->bitmap_top + face->glyph->bitmap.rows,
		face->glyph->advance.x >> 6,
	}.Translate(x, y);
}

Sqex::FontCsv::FreeTypeFont::FtFaceContextMgr::FtFaceContextMgr(const FreeTypeFont& owner, FT_Face face)
	: m_owner(owner)
	, m_face(face) {
}

Sqex::FontCsv::FreeTypeFont::FtFaceContextMgr::~FtFaceContextMgr() {
	try {
		auto lock = std::lock_guard(m_owner.m_pImpl->LibraryMtx);
		for (auto& slot : m_owner.m_pImpl->FaceSlots) {
			if (!slot) {
				slot = m_face;
				return;
			}
		}
		if (m_owner.m_pImpl->FaceSlots.size() < m_owner.m_pImpl->CoreCount) {
			m_owner.m_pImpl->FaceSlots.emplace_back(m_face);
			return;
		}
		FTSucc(FT_Done_Face(m_face));
	} catch (...) {
		// empty
	}
}

FT_Library Sqex::FontCsv::FreeTypeFont::FtFaceContextMgr::GetLibrary() const {
	return m_owner.m_pImpl->Library;
}

Sqex::FontCsv::FreeTypeFont::FtFaceContextMgr Sqex::FontCsv::FreeTypeFont::GetFace() const {
	FT_Face face;
	{
		const auto lock = std::lock_guard(m_pImpl->LibraryMtx);
		for (auto& slot : m_pImpl->FaceSlots) {
			if (slot) {
				const auto val = slot;
				slot = nullptr;
				return FtFaceContextMgr(*this, val);
			}
		}
		FTSucc(FT_New_Face(m_pImpl->Library, m_pImpl->Path.string().c_str(), m_pImpl->FaceIndex, &face));
	}
	FTSucc(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(64 * m_pImpl->Size), 72, 72));
	return FtFaceContextMgr(*this, face);
}
