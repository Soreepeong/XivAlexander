#ifndef _XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_

#ifndef FT2BUILD_H_
#include <ft2build.h>
#endif

#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_OUTLINE_H 
#include FT_GLYPH_H
#include FT_TRUETYPE_TABLES_H\

#include <filesystem>

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"
#include "../Internal/TrueTypeUtils.h"

namespace XivRes::FontGenerator {
	static FT_Error SuccessOrThrow(FT_Error error, std::initializer_list<FT_Error> acceptables = {}) {
		if (!error)
			return error;

		for (const auto& er : acceptables) {
			if (er == error)
				return error;
		}

		throw std::runtime_error(std::format("FreeType Error: 0x{:x}", error));
	}

	class FreeTypeFixedSizeFont : public DefaultAbstractFixedSizeFont {
		class FreeTypeLibraryWrapper {
			FT_Library m_library{};
			std::mutex m_mtx;

		public:
			FreeTypeLibraryWrapper() {
				SuccessOrThrow(FT_Init_FreeType(&m_library));
			}

			FreeTypeLibraryWrapper(FreeTypeLibraryWrapper&&) = delete;
			FreeTypeLibraryWrapper(const FreeTypeLibraryWrapper&) = delete;
			FreeTypeLibraryWrapper& operator=(FreeTypeLibraryWrapper&&) = delete;
			FreeTypeLibraryWrapper& operator=(const FreeTypeLibraryWrapper&) = delete;

			~FreeTypeLibraryWrapper() {
				SuccessOrThrow(FT_Done_FreeType(m_library));
			}

			FT_Library operator*() const {
				return m_library;
			}

			std::mutex& Mutex() {
				return m_mtx;
			}
		};

		class FreeTypeFontTable {
			std::vector<uint8_t> m_buf;

		public:
			FreeTypeFontTable(FT_Face face, uint32_t tag)  {
				FT_ULong len = 0;
				if (SuccessOrThrow(FT_Load_Sfnt_Table(face, tag, 0, nullptr, &len), { FT_Err_Table_Missing }))
					return;

				m_buf.resize(len);
				if (SuccessOrThrow(FT_Load_Sfnt_Table(face, tag, 0, &m_buf[0], &len), { FT_Err_Table_Missing }))
					return;
			}

			operator bool() const {
				return !m_buf.empty();
			}

			template<typename T = uint8_t>
			std::span<const T> GetSpan() const {
				if (m_buf.empty())
					return {};

				return { reinterpret_cast<const T*>(&m_buf[0]), m_buf.size() / sizeof T };
			}
		};

	public:
		struct CreateStruct {
			int LoadFlags = FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT;

			std::wstring GetLoadFlagsString() const {
				std::wstring res;
				if (LoadFlags & FT_LOAD_NO_HINTING) res += L", no hinting";
				if (LoadFlags & FT_LOAD_NO_BITMAP) res += L", no bitmap";
				if (LoadFlags & FT_LOAD_FORCE_AUTOHINT) res += L", force autohint";
				if (LoadFlags & FT_LOAD_NO_AUTOHINT) res += L", no autohint";

				if (res.empty())
					return L"Default";
				return res.substr(2);
			}
		};

	private:
		class FreeTypeFaceWrapper {
			struct InfoStruct {
				std::vector<uint8_t> Data;
				std::set<char32_t> Characters;
				std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
				std::map<char32_t, GlyphMetrics> AllGlyphMetrics;
				std::vector<uint8_t> GammaTable;
				CreateStruct Params{};
				int FaceIndex = 0;
				float Size = 0.f;
			};

			std::shared_ptr<FreeTypeLibraryWrapper> m_library;
			std::shared_ptr<const InfoStruct> m_info;
			FT_Face m_face{};

		public:
			FreeTypeFaceWrapper() = default;

			FreeTypeFaceWrapper(std::vector<uint8_t> data, int faceIndex, float size, float gamma, CreateStruct createStruct)
				: m_library(std::make_shared<FreeTypeLibraryWrapper>()) {

				auto info = std::make_shared<InfoStruct>();
				info->Data = std::move(data);
				info->Size = size;
				info->GammaTable = Internal::BitmapCopy::CreateGammaTable(gamma);
				info->Params = createStruct;
				info->FaceIndex = faceIndex;
				info->Params.LoadFlags &= (0 |
					FT_LOAD_NO_HINTING |
					FT_LOAD_NO_BITMAP |
					FT_LOAD_FORCE_AUTOHINT |
					FT_LOAD_NO_AUTOHINT);

				m_face = CreateFace(*m_library, *info);
				FT_UInt glyphIndex;
				for (char32_t c = FT_Get_First_Char(m_face, &glyphIndex); glyphIndex; c = FT_Get_Next_Char(m_face, c, &glyphIndex))
					info->Characters.insert(c);

				FreeTypeFontTable kernDataRef(m_face, Internal::TrueType::Kern::DirectoryTableTag.ReverseNativeValue);
				FreeTypeFontTable gposDataRef(m_face, Internal::TrueType::Gpos::DirectoryTableTag.ReverseNativeValue);
				FreeTypeFontTable cmapDataRef(m_face, Internal::TrueType::Cmap::DirectoryTableTag.ReverseNativeValue);
				Internal::TrueType::Kern::View kern(kernDataRef.GetSpan<char>());
				Internal::TrueType::Gpos::View gpos(gposDataRef.GetSpan<char>());
				Internal::TrueType::Cmap::View cmap(cmapDataRef.GetSpan<char>());
				if (cmap && (kern || gpos)) {
					const auto cmapVector = cmap.GetGlyphToCharMap();

					if (kern)
						info->KerningPairs = kern.Parse(cmapVector);

					if (gpos) {
						const auto pairs = gpos.ExtractAdvanceX(cmapVector);
						// do not overwrite
						info->KerningPairs.insert(pairs.begin(), pairs.end());
					}

					for (auto it = info->KerningPairs.begin(); it != info->KerningPairs.end(); ) {
						it->second = static_cast<int>(it->second * info->Size / static_cast<float>(m_face->size->face->units_per_EM));
						if (it->second)
							++it;
						else
							it = info->KerningPairs.erase(it);
					}
				}

				m_info = std::move(info);
			}

			FreeTypeFaceWrapper(FreeTypeFaceWrapper&& r) noexcept
				: m_library(std::move(r.m_library))
				, m_info(std::move(r.m_info))
				, m_face(r.m_face) {

				r.m_face = nullptr;
			}

			FreeTypeFaceWrapper(const FreeTypeFaceWrapper& r)
				: m_library(r.m_library)
				, m_info(r.m_info) {

				if (r.m_face)
					m_face = CreateFace(*m_library, *m_info);
			}

			FreeTypeFaceWrapper& operator=(FreeTypeFaceWrapper&& r) noexcept {
				if (this == &r)
					return *this;

				*this = nullptr;

				m_library = std::move(r.m_library);
				m_info = std::move(r.m_info);
				m_face = r.m_face;
				r.m_face = nullptr;

				return *this;
			}

			FreeTypeFaceWrapper& operator=(const FreeTypeFaceWrapper& r) {
				if (this == &r)
					return *this;

				const auto face = CreateFace(*r.m_library, *r.m_info);

				*this = nullptr;

				m_library = r.m_library;
				m_info = r.m_info;
				m_face = r.m_face;

				return *this;
			}

			FreeTypeFaceWrapper& operator=(const std::nullptr_t&) {
				if (!m_face)
					return *this;

				{
					const auto lock = std::lock_guard(m_library->Mutex());
					SuccessOrThrow(FT_Done_Face(m_face));
				}

				m_face = nullptr;
				m_info = nullptr;
				m_library = nullptr;

				return *this;
			}

			~FreeTypeFaceWrapper() {
				*this = nullptr;
			}

			FT_Face operator*() const {
				return m_face;
			}

			FT_Face operator->() const {
				return m_face;
			}

			int GetCharIndex(char32_t codepoint) const {
				return FT_Get_Char_Index(m_face, codepoint);
			}

			void LoadGlyph(int glyphIndex, bool bRender = false) const {
				if (m_face->glyph->glyph_index == glyphIndex && !bRender)
					return;

				SuccessOrThrow(FT_Load_Glyph(m_face, glyphIndex, m_info->Params.LoadFlags | (bRender ? FT_LOAD_RENDER : 0)));
			}

			FreeTypeLibraryWrapper& GetLibrary() const {
				return *m_library;
			}

			float GetSize() const {
				return m_info->Size;
			}

			std::span<const uint8_t> GetGammaTable() const {
				return std::span(m_info->GammaTable);
			}

			const std::set<char32_t>& GetAllCharacters() const {
				return m_info->Characters;
			}

			const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const {
				return m_info->KerningPairs;
			}

			int GetLoadFlags() const {
				return m_info->Params.LoadFlags;
			}

			std::span<const uint8_t> GetRawData() const {
				return m_info->Data;
			}

		private:
			static FT_Face CreateFace(FreeTypeLibraryWrapper& m_library, const InfoStruct& info) {
				FT_Face face;
				const auto lock = std::lock_guard(m_library.Mutex());
				SuccessOrThrow(FT_New_Memory_Face(*m_library, &info.Data[0], static_cast<FT_Long>(info.Data.size()), info.FaceIndex, &face));
				try {
					SuccessOrThrow(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(64.f * info.Size), 72, 72));
					return face;
				} catch (...) {
					SuccessOrThrow(FT_Done_Face(face));
					throw;
				}
			}
		};

		class FreeTypeBitmapWrapper {
			FreeTypeLibraryWrapper& m_library;
			FT_Bitmap m_bitmap;

		public:
			FreeTypeBitmapWrapper(FreeTypeLibraryWrapper& library)
				: m_library(library) {
				FT_Bitmap_Init(&m_bitmap);
			}

			FreeTypeBitmapWrapper(FreeTypeBitmapWrapper&&) = delete;
			FreeTypeBitmapWrapper(const FreeTypeBitmapWrapper&) = delete;
			FreeTypeBitmapWrapper& operator=(FreeTypeBitmapWrapper&&) = delete;
			FreeTypeBitmapWrapper& operator=(const FreeTypeBitmapWrapper&) = delete;

			~FreeTypeBitmapWrapper() {
				const auto lock = std::lock_guard(m_library.Mutex());
				SuccessOrThrow(FT_Bitmap_Done(*m_library, &m_bitmap));
			}

			FT_Bitmap& operator*() {
				return m_bitmap;
			}

			FT_Bitmap* operator->() {
				return &m_bitmap;
			}

			void ConvertFrom(const FT_Bitmap& source, int alignment) {
				SuccessOrThrow(FT_Bitmap_Convert(*m_library, &source, &m_bitmap, alignment));
				switch (m_bitmap.num_grays) {
					case 2:
						for (auto& b : GetBuffer())
							b = b ? 255 : 0;
						break;

					case 4:
						for (auto& b : GetBuffer())
							b = b * 85;
						break;

					case 16:
						for (auto& b : GetBuffer())
							b = b * 17;
						break;

					case 256:
						break;

					default:
						throw std::runtime_error("Invalid num_grays");
				}
			}

			std::span<uint8_t> GetBuffer() const {
				return std::span(m_bitmap.buffer, m_bitmap.rows * m_bitmap.pitch);
			}
		};

		FreeTypeFaceWrapper m_face;

	public:
		FreeTypeFixedSizeFont(const std::filesystem::path& path, int faceIndex, float size, float gamma, CreateStruct createStruct)
			: FreeTypeFixedSizeFont(ReadStreamIntoVector<uint8_t>(FileStream(path)), faceIndex, size, gamma, createStruct) {}

		FreeTypeFixedSizeFont(IStream& stream, int faceIndex, float fSize, float gamma, CreateStruct createStruct)
			: m_face(ReadStreamIntoVector<uint8_t>(stream), faceIndex, fSize, gamma, createStruct) {}

		FreeTypeFixedSizeFont(std::vector<uint8_t> data, int faceIndex, float fSize, float gamma, CreateStruct createStruct)
			: m_face(std::move(data), faceIndex, fSize, gamma, createStruct) {}

		FreeTypeFixedSizeFont(FreeTypeFixedSizeFont&& r) = default;
		FreeTypeFixedSizeFont(const FreeTypeFixedSizeFont& r) = default;
		FreeTypeFixedSizeFont& operator=(FreeTypeFixedSizeFont&& r) = default;
		FreeTypeFixedSizeFont& operator=(const FreeTypeFixedSizeFont& r) = default;

		std::string GetFamilyName() const override {
			FreeTypeFontTable nameDataRef(*m_face, Internal::TrueType::Name::DirectoryTableTag.ReverseNativeValue);
			Internal::TrueType::Name::View name(nameDataRef.GetSpan<char>());
			if (!name)
				return {};

			return name.GetPreferredFamilyName<std::string>(0);
		}

		std::string GetSubfamilyName() const override {
			FreeTypeFontTable nameDataRef(*m_face, Internal::TrueType::Name::DirectoryTableTag.ReverseNativeValue);
			Internal::TrueType::Name::View name(nameDataRef.GetSpan<char>());
			if (!name)
				return {};

			return name.GetPreferredSubfamilyName<std::string>(0);
		}

		float GetSize() const override {
			return m_face.GetSize();
		}

		int GetAscent() const override {
			return (m_face->size->metrics.ascender + 63) / 64;
		}

		int GetLineHeight() const override {
			return (m_face->size->metrics.height + 63) / 64;
		}

		const std::set<char32_t>& GetAllCodepoints() const override {
			return m_face.GetAllCharacters();
		}

		const void* GetBaseFontGlyphUniqid(char32_t c) const override {
			return m_face.GetAllCharacters().contains(c) ? &m_face.GetRawData()[c] : nullptr;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			const auto glyphIndex = m_face.GetCharIndex(codepoint);
			if (!glyphIndex)
				return false;

			m_face.LoadGlyph(glyphIndex, false);
			gm = GlyphMetricsFromCurrentGlyph();
			return true;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			return m_face.GetAllKerningPairs();
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			if (const auto it = m_face.GetAllKerningPairs().find(std::make_pair(left, right)); it != m_face.GetAllKerningPairs().end())
				return gm.AdvanceX + it->second;
			
			return gm.AdvanceX;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			const auto glyphIndex = m_face.GetCharIndex(codepoint);
			if (!glyphIndex)
				return false;

			m_face.LoadGlyph(glyphIndex, true);
			auto dest = GlyphMetricsFromCurrentGlyph(drawX, drawY);
			auto src = dest;
			src.Translate(-src.X1, -src.Y1);
			src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
			if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
				return true;

			FreeTypeBitmapWrapper bitmapWrapper(m_face.GetLibrary());
			bitmapWrapper.ConvertFrom(m_face->glyph->bitmap, 1);

			Internal::BitmapCopy::ToRGBA8888()
				.From(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithBackgroundColor(bgColor)
				.WithGammaTable(m_face.GetGammaTable())
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			const auto glyphIndex = m_face.GetCharIndex(codepoint);
			if (!glyphIndex)
				return false;

			m_face.LoadGlyph(glyphIndex, true);
			auto dest = GlyphMetricsFromCurrentGlyph(drawX, drawY);
			auto src = dest;
			src.Translate(-src.X1, -src.Y1);
			src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
			if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
				return true;

			FreeTypeBitmapWrapper bitmapWrapper(m_face.GetLibrary());
			bitmapWrapper.ConvertFrom(m_face->glyph->bitmap, 1);

			Internal::BitmapCopy::ToL8()
				.From(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, stride, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithForegroundOpacity(fgOpacity)
				.WithBackgroundColor(bgColor)
				.WithBackgroundOpacity(bgOpacity)
				.WithGammaTable(m_face.GetGammaTable())
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return std::make_shared<FreeTypeFixedSizeFont>(*this);
		}

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override {
			return this;
		}

	private:
		GlyphMetrics GlyphMetricsFromCurrentGlyph(int x = 0, int y = 0) const {
			GlyphMetrics src{
				.X1 = x + m_face->glyph->bitmap_left,
				.Y1 = y + GetAscent() - m_face->glyph->bitmap_top,
				.X2 = src.X1 + static_cast<int>(m_face->glyph->bitmap.width),
				.Y2 = src.Y1 + static_cast<int>(m_face->glyph->bitmap.rows),
				.AdvanceX = (m_face->glyph->advance.x + 63) / 64,
			};
			return src;
		}
	};
}

#endif
