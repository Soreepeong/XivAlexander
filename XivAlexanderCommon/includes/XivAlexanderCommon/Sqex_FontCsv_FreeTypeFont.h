#pragma once

#include "Sqex_FontCsv_SeCompatibleDrawableFont.h"
#include "Sqex_FontCsv_SeCompatibleFont.h"
#include "Sqex_Texture.h"

namespace Sqex::FontCsv {
	inline void FTSucc(FT_Error error) {
		if (error)
			throw std::runtime_error(std::format("FreeType Error: {}", error));
	}

	class FreeTypeFont : public virtual SeCompatibleFont {
	protected:
		const FT_Int32 m_loadFlags;

	private:
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		FreeTypeFont(std::filesystem::path path, int faceIndex, float size, FT_Int32 loadFlags = FT_LOAD_DEFAULT);
		FreeTypeFont(const wchar_t* fontName,
			float size,
			DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT::DWRITE_FONT_WEIGHT_REGULAR,
			DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH::DWRITE_FONT_STRETCH_NORMAL,
			DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE::DWRITE_FONT_STYLE_NORMAL,
			FT_Int32 loadFlags = FT_LOAD_DEFAULT);
		~FreeTypeFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] SSIZE_T GetCharacterWidth(char32_t c) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t Descent() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		class FtFaceContextMgr {
			const FreeTypeFont& m_owner;
			FT_Face m_face;

		public:
			FtFaceContextMgr(const FreeTypeFont& owner, FT_Face face);
			~FtFaceContextMgr();

			FT_Library GetLibrary() const;

			FT_Face& operator*() {
				return m_face;
			}

			FT_Face operator->() {
				return m_face;
			}
		};

		FtFaceContextMgr GetFace() const;
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
			auto face = GetFace();
			FTSucc(FT_Load_Char(*face, c, m_loadFlags | FT_LOAD_RENDER));

			if (face->glyph->glyph_index == 0)
				return { true };

			FT_Bitmap target;
			auto temporaryTarget = false;
			const auto targetCleanup = CallOnDestruction([&face, &target, &temporaryTarget]() {
				if (temporaryTarget)
					FTSucc(FT_Bitmap_Done(face.GetLibrary(), &target));
			});
			if (face->glyph->bitmap.width != face->glyph->bitmap.pitch) {
				FT_Bitmap_Init(&target);
				FTSucc(FT_Bitmap_Convert(face.GetLibrary(), &face->glyph->bitmap, &target, 1));
				temporaryTarget = true;
			} else
				target = face->glyph->bitmap;

			const auto ascent = static_cast<SSIZE_T>(face->size->metrics.ascender) / 64;
			const auto bbox = GlyphMeasurement{
				false,
				face->glyph->bitmap_left,
				ascent - face->glyph->bitmap_top,
				static_cast<SSIZE_T>(0) + face->glyph->bitmap_left + target.width,
				ascent - face->glyph->bitmap_top + target.rows,
				face->glyph->advance.x >> 6,
			}.Translate(x, y);
			const auto srcBuf = std::span(target.buffer, bbox.Area());
			
			if (!srcBuf.empty()) {
				const auto destWidth = static_cast<SSIZE_T>(to->Width());
				const auto destHeight = static_cast<SSIZE_T>(to->Height());
				const auto srcWidth = bbox.Width();
				const auto srcHeight = bbox.Height();

				GlyphMeasurement src = { false, 0, 0, srcWidth, srcHeight };
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
