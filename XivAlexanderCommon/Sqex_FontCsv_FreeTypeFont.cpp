#include "pch.h"
#include "Sqex_FontCsv_FreeTypeFont.h"

#include "Sqex_FontCsv_DirectWriteFont.h"

struct Sqex::FontCsv::FreeTypeFont::Implementation {
	class LibraryAccessor;

	FreeTypeFont* const this_;
	const Win32::File File;
	const Win32::FileMapping FileMapping;
	const Win32::FileMapping::View FileMappingView;
	const int FaceIndex;
	const float Size;

private:
	class FtLibraryWrapper {
		FT_Library m_library{};
		friend class LibraryAccessor;

	public:
		FtLibraryWrapper() {
			Succ(FT_Init_FreeType(&m_library));
		}

		~FtLibraryWrapper() {
			Must(FT_Done_FreeType(m_library));
		}

		[[nodiscard]] FT_Library GetLibraryUnprotected() const {
			return m_library;
		}
	};

	FtLibraryWrapper Library;
	std::mutex LibraryMtx;

public:
	std::vector<FT_Face> FaceSlots;
	std::mutex FaceSlotMtx;

	const std::vector<char32_t> CharacterList;
	const std::map<std::pair<char32_t, char32_t>, SSIZE_T> KerningMap;

	CallOnDestruction::Multiple Cleanup;

	Implementation(FreeTypeFont* this_, const std::filesystem::path& path, int faceIndex, float size)
		: this_(this_)
		, File(Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0))
		, FileMapping(Win32::FileMapping::Create(File))
		, FileMappingView(Win32::FileMapping::View::Create(FileMapping))
		, FaceIndex(faceIndex)
		, Size(size)
		, FaceSlots(Win32::GetCoreCount())
		, CharacterList([face = LibraryAccessor(this).NewFace()]() {
			std::vector<char32_t> result;
			FT_UInt glyphIndex;
			for (char32_t c = FT_Get_First_Char(*face, &glyphIndex); glyphIndex; c = FT_Get_Next_Char(*face, c, &glyphIndex))
				result.push_back(c);
			return result;
		}())
		// defer work to DirectWrite, as FreeType does not offer an API to retrieve all kerning pairs
		, KerningMap(DirectWriteFont(path, FaceIndex, Size).GetKerningTable()) {
	}

	~Implementation() = default;

	class LibraryAccessor {
		friend struct Implementation;
		Implementation* const m_pImpl;
		std::lock_guard<decltype(LibraryMtx)> const m_lock;

		LibraryAccessor(Implementation* impl)
			: m_pImpl(impl)
			, m_lock(impl->LibraryMtx) {
		}

	public:
		operator FT_Library() const {
			return m_pImpl->Library.m_library;
		}

		FtFaceCtxMgr NewFace() {
			FT_Face face;
			const auto view = m_pImpl->FileMappingView.AsSpan<uint8_t>();
			Succ(FT_New_Memory_Face(m_pImpl->Library.m_library, &view[0], static_cast<FT_Long>(view.size_bytes()), m_pImpl->FaceIndex, &face));

			try {
				Succ(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(64 * m_pImpl->Size), 72, 72));
				return FtFaceCtxMgr(m_pImpl->this_, face);
			} catch (...) {
				Succ(FT_Done_Face(face));
				throw;
			}
		}

		// ReSharper disable once CppMemberFunctionMayBeStatic
		// ^ FT_Done_Face requires synchronized access to underlying library
		void FreeFace(FT_Face face) const noexcept {
			Must(FT_Done_Face(face));
		}
	};

	[[nodiscard]] LibraryAccessor GetLibrary() {
		return {this};
	}

	[[nodiscard]] FT_Library GetLibraryUnprotected() const {
		return Library.GetLibraryUnprotected();
	}
};

void Sqex::FontCsv::FreeTypeFont::ShowFreeTypeErrorAndTerminate(FT_Error error) {
	Win32::MessageBoxF(nullptr, MB_ICONERROR, L"Sqex::FontCsv::FreeTypeFont", L"error: FT_Error {}", error);
	std::terminate();
}

Sqex::FontCsv::FreeTypeFont::FreeTypeFont(const std::filesystem::path& path, int faceIndex, float size, FT_Int32 loadFlags)
	: m_loadFlags(loadFlags)
	, m_pImpl(std::make_unique<Implementation>(this, path, faceIndex, size)) {
}

Sqex::FontCsv::FreeTypeFont::FreeTypeFont(const wchar_t* fontName, float size, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, FT_Int32 loadFlags)
	: m_loadFlags(loadFlags)
	, m_pImpl([&]() {
		auto [path, faceIndex] = DirectWriteFont(fontName, size, weight, stretch, style).GetFontFile();
		return std::make_unique<Implementation>(this, std::move(path), faceIndex, size);
	}()) {
}

Sqex::FontCsv::FreeTypeFont::~FreeTypeFont() {
	for (const auto& slot : m_pImpl->FaceSlots) {
		if (slot)
			Must(FT_Done_Face(slot));
	}
}

bool Sqex::FontCsv::FreeTypeFont::HasCharacter(char32_t c) const {
	return FT_Get_Char_Index(*GetFace(), c);
}

float Sqex::FontCsv::FreeTypeFont::Size() const {
	return m_pImpl->Size;
}

const std::vector<char32_t>& Sqex::FontCsv::FreeTypeFont::GetAllCharacters() const {
	return m_pImpl->CharacterList;
}

uint32_t Sqex::FontCsv::FreeTypeFont::Ascent() const {
	return GetFace().Ascent();
}

uint32_t Sqex::FontCsv::FreeTypeFont::LineHeight() const {
	return GetFace().LineHeight();
}

const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& Sqex::FontCsv::FreeTypeFont::GetKerningTable() const {
	return m_pImpl->KerningMap;
}

Sqex::FontCsv::GlyphMeasurement Sqex::FontCsv::FreeTypeFont::Measure(SSIZE_T x, SSIZE_T y, char32_t c) const {
	return GetFace(c).ToMeasurement(x, y);
}

Sqex::FontCsv::FreeTypeFont::FtFaceCtxMgr::FtFaceCtxMgr(const FreeTypeFont* impl, FT_Face face)
	: m_owner(impl)
	, m_face(face) {
}

Sqex::FontCsv::FreeTypeFont::FtFaceCtxMgr::~FtFaceCtxMgr() {
	if (!m_face)
		return;
	for (const auto lock = std::lock_guard(m_owner->m_pImpl->FaceSlotMtx);
		auto& slot : m_owner->m_pImpl->FaceSlots) {
		if (!slot) {
			slot = m_face;
			return;
		}
	}

	m_owner->m_pImpl->GetLibrary().FreeFace(m_face);
}

FT_Library Sqex::FontCsv::FreeTypeFont::FtFaceCtxMgr::GetLibraryUnprotected() const {
	return m_owner->m_pImpl->GetLibraryUnprotected();
}

Sqex::FontCsv::FreeTypeFont::FtFaceCtxMgr Sqex::FontCsv::FreeTypeFont::GetFace(char32_t c, FT_Int32 additionalFlags) const {
	for (const auto lock = std::lock_guard(m_pImpl->FaceSlotMtx);
		auto& slot : m_pImpl->FaceSlots) {
		if (slot) {
			const auto face = slot;
			if (c != std::numeric_limits<decltype(c)>::max())
				Succ(FT_Load_Char(face, c, m_loadFlags | additionalFlags));
			slot = nullptr;
			return FtFaceCtxMgr(this, face);
		}
	}

	auto face = m_pImpl->GetLibrary().NewFace();
	if (c != std::numeric_limits<decltype(c)>::max())
		Succ(FT_Load_Char(*face, c, m_loadFlags | additionalFlags));
	return face;
}
