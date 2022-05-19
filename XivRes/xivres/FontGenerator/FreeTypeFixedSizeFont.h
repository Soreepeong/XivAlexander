#ifndef _XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_FREETYPEFIXEDSIZEFONT_H_

#ifndef FT2BUILD_H_
#pragma error("Freetype must be included to use this header file.")
#endif

#include <filesystem>

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {

	namespace FreeTypeInternal {
		static void SuccessOrThrow(FT_Error error) {
			if (error)
				throw std::runtime_error(std::format("FreeType Error: {:x}", error));
		}

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

			std::unique_lock<std::mutex> Lock() {
				return std::unique_lock<std::mutex>(m_mtx);
			}
		};

		class FreeTypeFaceWrapper {
			std::shared_ptr<FreeTypeLibraryWrapper> m_library;
			std::shared_ptr<std::vector<uint8_t>> m_data;
			std::shared_ptr<std::set<char32_t>> m_characters;
			int m_nFaceIndex{};
			float m_fSize{};
			int m_nLoadFlags{};
			FT_Face m_face{};

		public:
			FreeTypeFaceWrapper() = default;

			FreeTypeFaceWrapper(std::shared_ptr<std::vector<uint8_t>> data, int faceIndex, float fSize, int nLoadFlags)
				: m_library(std::make_shared<FreeTypeLibraryWrapper>())
				, m_data(std::move(data))
				, m_characters(std::make_shared<std::set<char32_t>>())
				, m_nFaceIndex(faceIndex)
				, m_fSize(fSize)
				, m_nLoadFlags(nLoadFlags) {

				const auto lock = m_library->Lock();
				SuccessOrThrow(FT_New_Memory_Face(**m_library, &(*m_data)[0], static_cast<FT_Long>(m_data->size()), m_nFaceIndex, &m_face));
				try {
					SuccessOrThrow(FT_Set_Char_Size(m_face, 0, static_cast<FT_F26Dot6>(64.f * m_fSize), 72, 72));

					FT_UInt glyphIndex;
					for (char32_t c = FT_Get_First_Char(m_face, &glyphIndex); glyphIndex; c = FT_Get_Next_Char(m_face, c, &glyphIndex))
						m_characters->insert(c);

				} catch (...) {
					SuccessOrThrow(FT_Done_Face(m_face));
					throw;
				}
			}

			FreeTypeFaceWrapper(FreeTypeFaceWrapper&& r) noexcept
				: m_library(std::move(r.m_library))
				, m_data(std::move(r.m_data))
				, m_characters(std::move(r.m_characters))
				, m_nFaceIndex(std::move(r.m_nFaceIndex))
				, m_fSize(std::move(r.m_fSize))
				, m_nLoadFlags(std::move(r.m_nLoadFlags))
				, m_face(std::move(r.m_face)) {

				r.m_nFaceIndex = 0;
				r.m_fSize = 0.f;
				r.m_nLoadFlags = 0;
				r.m_face = nullptr;
			}

			FreeTypeFaceWrapper(const FreeTypeFaceWrapper& r)
				: m_library(r.m_library)
				, m_data(r.m_data)
				, m_characters(r.m_characters)
				, m_nFaceIndex(r.m_nFaceIndex)
				, m_fSize(r.m_fSize)
				, m_nLoadFlags(r.m_nLoadFlags) {

				if (!m_library)
					return;

				const auto lock = m_library->Lock();
				SuccessOrThrow(FT_New_Memory_Face(**m_library, &(*m_data)[0], static_cast<FT_Long>(m_data->size()), m_nFaceIndex, &m_face));
				try {
					SuccessOrThrow(FT_Set_Char_Size(m_face, 0, static_cast<FT_F26Dot6>(64.f * m_fSize), 72, 72));
				} catch (...) {
					SuccessOrThrow(FT_Done_Face(m_face));
					throw;
				}
			}

			FreeTypeFaceWrapper& operator=(FreeTypeFaceWrapper&& r) noexcept {
				if (this == &r)
					return *this;

				*this = nullptr;
				m_library = std::move(r.m_library);
				m_data = std::move(r.m_data);
				m_characters = std::move(r.m_characters);
				m_nFaceIndex = std::move(r.m_nFaceIndex);
				m_fSize = std::move(r.m_fSize);
				m_nLoadFlags = std::move(r.m_nLoadFlags);
				m_face = std::move(r.m_face);
				r = nullptr;
				return *this;
			}

			FreeTypeFaceWrapper& operator=(const FreeTypeFaceWrapper& r) {
				if (this == &r)
					return *this;

				const auto lock = r.m_library->Lock();
				FT_Face face;
				SuccessOrThrow(FT_New_Memory_Face(**r.m_library, &(*r.m_data)[0], static_cast<FT_Long>(r.m_data->size()), r.m_nFaceIndex, &face));
				try {
					SuccessOrThrow(FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(64.f * r.m_fSize), 72, 72));
				} catch (...) {
					SuccessOrThrow(FT_Done_Face(face));
					throw;
				}

				*this = nullptr;
				m_library = r.m_library;
				m_data = r.m_data;
				m_characters = r.m_characters;
				m_nFaceIndex = r.m_nFaceIndex;
				m_fSize = r.m_fSize;
				m_nLoadFlags = r.m_nLoadFlags;
				m_face = face;
				return *this;
			}

			FreeTypeFaceWrapper& operator=(const std::nullptr_t&) {
				if (!m_face)
					return *this;

				{
					const auto lock = m_library->Lock();
					SuccessOrThrow(FT_Done_Face(m_face));
				}

				m_nFaceIndex = 0;
				m_fSize = 0.f;
				m_nLoadFlags = 0;
				m_characters = nullptr;
				m_face = nullptr;
				m_data = nullptr;
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

				SuccessOrThrow(FT_Load_Glyph(m_face, glyphIndex, m_nLoadFlags | (bRender ? FT_LOAD_RENDER : 0)));
			}

			FreeTypeLibraryWrapper& GetLibrary() const {
				return *m_library;
			}

			float GetSize() const {
				return m_fSize;
			}

			void SetSize(float newSize) {
				SuccessOrThrow(FT_Set_Char_Size(m_face, 0, static_cast<FT_F26Dot6>(64.f * newSize), 72, 72));
				m_fSize = newSize;
			}

			const std::set<char32_t>& GetAllCharacters() const {
				return *m_characters;
			}

			int GetLoadFlags() const {
				return m_nLoadFlags;
			}

			std::span<const uint8_t> GetRawData() const {
				return *m_data;
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
				const auto lock = m_library.Lock();
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
	}

	class FreeTypeFixedSizeFont : public DefaultAbstractFixedSizeFont {
		FreeTypeInternal::FreeTypeFaceWrapper m_face;
		std::array<uint8_t, 256> m_gammaTable;

	public:
		FreeTypeFixedSizeFont(const std::filesystem::path& path, int faceIndex, float fSize, int nLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)
			: FreeTypeFixedSizeFont(std::make_shared<std::vector<uint8_t>>(ReadStreamIntoVector<uint8_t>(FileStream(path))), faceIndex, fSize, nLoadFlags) {}

		FreeTypeFixedSizeFont(std::shared_ptr<std::vector<uint8_t>> data, int faceIndex, float fSize, int nLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)
			: m_face(std::move(data), faceIndex, fSize, nLoadFlags)
			, m_gammaTable(WindowsGammaTable) {}

		FreeTypeFixedSizeFont(FreeTypeFixedSizeFont&& r)
			: m_face(std::move(r.m_face))
			, m_gammaTable(r.m_gammaTable) {
			r.m_gammaTable = WindowsGammaTable;
		}

		FreeTypeFixedSizeFont(const FreeTypeFixedSizeFont& r)
			: m_face(r.m_face)
			, m_gammaTable(r.m_gammaTable) {}

		FreeTypeFixedSizeFont& operator=(FreeTypeFixedSizeFont&& r) {
			m_face = std::move(r.m_face);
			m_gammaTable = r.m_gammaTable;
			r.m_gammaTable = WindowsGammaTable;
			return *this;
		}

		FreeTypeFixedSizeFont& operator=(const FreeTypeFixedSizeFont& r) {
			m_face = r.m_face;
			m_gammaTable = r.m_gammaTable;
			return *this;
		}

		float GetSize() const override {
			return m_face.GetSize();
		}

		int GetAscent() const override {
			return m_face->size->metrics.ascender / 64;
		}

		int GetLineHeight() const override {
			return m_face->size->metrics.height / 64;
		}

		size_t GetCodepointCount() const override {
			return m_face.GetAllCharacters().size();
		}

		std::set<char32_t> GetAllCodepoints() const override {
			return m_face.GetAllCharacters();
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			const auto glyphIndex = m_face.GetCharIndex(codepoint);
			if (!glyphIndex) {
				gm.Clear();
				return false;
			}

			m_face.LoadGlyph(glyphIndex);
			gm = GlyphMetricsFromCurrentGlyph();
			return true;
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			return m_face.GetAllCharacters().contains(c) ? &m_face.GetRawData()[c] : nullptr;
		}

		size_t GetKerningEntryCount() const override {
			return 0;  // TODO
		}

		std::map<std::pair<char32_t, char32_t>, int> GetKerningPairs() const override {
			return {};  // TODO
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			const auto glyphIndexRight = m_face.GetCharIndex(right);
			if (!glyphIndexRight)
				return gm.AdvanceX;

			const auto glyphIndexLeft = m_face.GetCharIndex(left);
			FT_Vector vec{};
			FreeTypeInternal::SuccessOrThrow(FT_Get_Kerning(*m_face, glyphIndexLeft, glyphIndexRight, 0, &vec));
			return gm.AdvanceX + (FT_IS_SCALABLE(*m_face) ? vec.x / 64 : vec.x);
		}

		const std::array<uint8_t, 256>& GetGammaTable() const override {
			return m_gammaTable;
		}

		void SetGammaTable(const std::array<uint8_t, 256>& gammaTable) override {
			m_gammaTable = gammaTable;
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

			FreeTypeInternal::FreeTypeBitmapWrapper bitmapWrapper(m_face.GetLibrary());
			bitmapWrapper.ConvertFrom(m_face->glyph->bitmap, 1);

			Internal::BitmapCopy()
				.From(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithBackgroundColor(bgColor)
				.WithGammaTable(m_gammaTable)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			const auto glyphIndex = m_face.GetCharIndex(codepoint);
			if (!glyphIndex)
				return false;

			m_face.LoadGlyph(glyphIndex, true);
			GlyphMetrics src{
				.Empty = false,
				.X1 = 0,
				.Y1 = 0,
				.X2 = static_cast<int>(m_face->glyph->bitmap.width),
				.Y2 = static_cast<int>(m_face->glyph->bitmap.rows),
			};
			auto dest = GlyphMetricsFromCurrentGlyph(drawX, drawY);
			src.AdjustToIntersection(dest, src.GetWidth(), src.GetHeight(), destWidth, destHeight);
			if (src.IsEffectivelyEmpty() || dest.IsEffectivelyEmpty())
				return true;

			FreeTypeInternal::FreeTypeBitmapWrapper bitmapWrapper(m_face.GetLibrary());
			bitmapWrapper.ConvertFrom(m_face->glyph->bitmap, 1);

			Internal::L8BitmapCopy()
				.From(bitmapWrapper->buffer, bitmapWrapper->pitch, bitmapWrapper->rows, 1, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithForegroundOpacity(fgOpacity)
				.WithBackgroundColor(bgColor)
				.WithBackgroundOpacity(bgOpacity)
				.WithGammaTable(m_gammaTable)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return std::make_shared<FreeTypeFixedSizeFont>(*this);
		}

	private:
		GlyphMetrics GlyphMetricsFromCurrentGlyph(int x = 0, int y = 0) const {
			GlyphMetrics src{
				.Empty = false,
				.X1 = x + m_face->glyph->bitmap_left,
				.Y1 = y + GetAscent() - m_face->glyph->bitmap_top,
				.X2 = src.X1 + static_cast<int>(m_face->glyph->bitmap.width),
				.Y2 = src.Y1 + static_cast<int>(m_face->glyph->bitmap.rows),
				.AdvanceX = FT_IS_SCALABLE(*m_face) ? m_face->glyph->advance.x / 64 : m_face->glyph->advance.x,
			};
			return src;
		}
	};
}

#endif
