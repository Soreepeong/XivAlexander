#pragma once

#include "Sqex_FontCsv_SeCompatibleDrawableFont.h"
#include "Sqex_FontCsv_SeCompatibleFont.h"
#include "Sqex_Texture.h"

namespace Sqex::FontCsv {

	class FreeTypeFont : public virtual SeCompatibleFont {
	protected:
		const FT_Int32 m_loadFlags;

		static void Succ(FT_Error error) {
			if (error)
				throw std::runtime_error(std::format("FreeType Error: {:x}", error));
		}

		__declspec(noreturn)
		static void ShowFreeTypeErrorAndTerminate(FT_Error error);

		static void Must(FT_Error error) noexcept {
			if (error)
				ShowFreeTypeErrorAndTerminate(error);
		}

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		FreeTypeFont(const std::filesystem::path& path, int faceIndex, float size, FT_Int32 loadFlags = FT_LOAD_DEFAULT);
		FreeTypeFont(const wchar_t* fontName,
			float size,
			DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR,
			DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL,
			DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL,
			FT_Int32 loadFlags = FT_LOAD_DEFAULT);
		~FreeTypeFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t LineHeight() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		class FtFaceCtxMgr {
			const FreeTypeFont* m_owner;
			FT_Face m_face;

		public:
			FtFaceCtxMgr(const FreeTypeFont* impl, FT_Face face);

			FtFaceCtxMgr(FtFaceCtxMgr&& r) noexcept
				: m_owner(r.m_owner)
				, m_face(r.m_face) {
				r.m_face = nullptr;
				r.m_owner = nullptr;
			}

			FtFaceCtxMgr& operator=(FtFaceCtxMgr&& r) noexcept {
				m_face = r.m_face;
				m_owner = r.m_owner;
				r.m_face = nullptr;
				r.m_owner = nullptr;
				return *this;
			}

			~FtFaceCtxMgr();

			[[nodiscard]] FT_Library GetLibraryUnprotected() const;

			FT_Face& operator*() {
				return m_face;
			}

			const FT_Face& operator*() const {
				return m_face;
			}

			FT_Face operator->() const {
				return m_face;
			}

			[[nodiscard]] uint32_t Ascent() const {
				return m_face->size->metrics.ascender / 64;
			}

			[[nodiscard]] uint32_t LineHeight() const {
				return m_face->size->metrics.height / 64;
			}

			[[nodiscard]] GlyphMeasurement ToMeasurement(SSIZE_T x, SSIZE_T y) const {
				if (m_face->glyph->glyph_index == 0)
					return {true};

				constexpr auto a = SSIZE_T();
				return GlyphMeasurement{
					.empty = false,
					.left = a + m_face->glyph->bitmap_left,
					.top = static_cast<SSIZE_T>(a + Ascent() - m_face->glyph->bitmap_top),
					.right = static_cast<SSIZE_T>(a + m_face->glyph->bitmap_left + m_face->glyph->bitmap.width),
					.bottom = static_cast<SSIZE_T>(a + Ascent() - m_face->glyph->bitmap_top + m_face->glyph->bitmap.rows),
					.advanceX = static_cast<SSIZE_T>(m_face->glyph->advance.x / 64 + m_owner->m_advanceWidthDelta),
				}.Translate(x, y);
			}
		};

		FtFaceCtxMgr GetFace(char32_t c = std::numeric_limits<char32_t>::max(), FT_Int32 additionalFlags = 0) const;
	};

	template<uint32_t Levels>
	uint32_t FreeTypeDrawingFont_GetEffectiveOpacity(const uint8_t& src) {
		return src * 255 / (Levels - 1);
	}

	// FreeType will return 128 if the bitmap got width of 1 pixel, instead of 1.
	// As long as it consistently returns 0 on 0, it might as well be treated as a boolean.
	template<>
	inline uint32_t FreeTypeDrawingFont_GetEffectiveOpacity<2>(const uint8_t& src) {
		return src ? 255 : 0;
	}

#pragma warning(push)
#pragma warning(disable: 4250)
	template<typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class FreeTypeDrawingFont : public FreeTypeFont, public SeCompatibleDrawableFont<DestPixFmt, OpacityType> {

	public:
		using FreeTypeFont::FreeTypeFont;

		using SeCompatibleDrawableFont<DestPixFmt, OpacityType>::Draw;

		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity, OpacityType bgOpacity) const override {
			const auto face = GetFace(c, FT_LOAD_RENDER);
			const auto bbox = face.ToMeasurement(x, y);
			if (bbox.empty)
				return bbox;

			FT_Bitmap target;
			auto temporaryTarget = false;
			const auto targetCleanup = CallOnDestruction([&face, &target, &temporaryTarget]() {
				if (temporaryTarget)
					Succ(FT_Bitmap_Done(face.GetLibraryUnprotected(), &target));
			});
			if (face->glyph->bitmap.width != face->glyph->bitmap.pitch) {
				FT_Bitmap_Init(&target);
				Succ(FT_Bitmap_Convert(face.GetLibraryUnprotected(), &face->glyph->bitmap, &target, 1));
				temporaryTarget = true;
			} else
				target = face->glyph->bitmap;

			const auto srcBuf = std::span(target.buffer, bbox.Area());

			if (!srcBuf.empty()) {
				const auto destWidth = static_cast<SSIZE_T>(to->Width());
				const auto destHeight = static_cast<SSIZE_T>(to->Height());
				const auto srcWidth = bbox.Width();
				const auto srcHeight = bbox.Height();

				GlyphMeasurement src = {false, 0, 0, srcWidth, srcHeight};
				auto dest = bbox;
				src.AdjustToIntersection(dest, srcWidth, srcHeight, destWidth, destHeight);

				if (!src.EffectivelyEmpty() && !dest.EffectivelyEmpty()) {
					auto destBuf = to->View<DestPixFmt>();
					switch (target.num_grays) {
						case 2:
							RgbBitmapCopy<uint8_t, FreeTypeDrawingFont_GetEffectiveOpacity<2>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
							break;
						case 4:
							RgbBitmapCopy<uint8_t, FreeTypeDrawingFont_GetEffectiveOpacity<4>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
							break;
						case 16:
							RgbBitmapCopy<uint8_t, FreeTypeDrawingFont_GetEffectiveOpacity<16>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
							break;
						case 256:
							RgbBitmapCopy<uint8_t, FreeTypeDrawingFont_GetEffectiveOpacity<256>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
							break;
						default:
							throw std::invalid_argument("invalid num_grays");
					}
				}
			}
			return bbox;
		}
	};
#pragma warning(pop)
}
