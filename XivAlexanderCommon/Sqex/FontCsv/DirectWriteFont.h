#pragma once
#include "XivAlexanderCommon/Sqex/FontCsv/BaseDrawableFont.h"

namespace Sqex::FontCsv {
	class DirectWriteFont : public virtual BaseFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	protected:
		static void Succ(HRESULT hr) {
			if (!SUCCEEDED(hr))
				throw Win32::Error(_com_error(hr));
		}

	public:
		DirectWriteFont(const wchar_t* fontName,
			float size,
			DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR,
			DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL,
			DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL,
			DWRITE_RENDERING_MODE renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC);
		DirectWriteFont(const std::filesystem::path& path,
			uint32_t faceIndex,
			float size,
			DWRITE_RENDERING_MODE renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC);
		~DirectWriteFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] GlyphMeasurement MaxBoundingBox() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t LineHeight() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using BaseFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

		void SetMeasureWithFreeType();
		GlyphMeasurement DrawCharacter(char32_t c, std::vector<uint8_t>& buf, bool draw) const;

		std::tuple<std::filesystem::path, int> GetFontFile() const;

	protected:
		class DwriteRenderBufferCtxMgr {
			Implementation* m_pImpl;
			std::unique_ptr<std::vector<uint8_t>> m_wrapper;

		public:
			DwriteRenderBufferCtxMgr(Implementation* impl, std::unique_ptr<std::vector<uint8_t>> wrapper)
				: m_pImpl(impl)
				, m_wrapper(std::move(wrapper)) {
			}

			DwriteRenderBufferCtxMgr(DwriteRenderBufferCtxMgr&& r) noexcept
				: m_pImpl(r.m_pImpl)
				, m_wrapper(std::move(r.m_wrapper)) {
				r.m_pImpl = nullptr;
			}

			DwriteRenderBufferCtxMgr& operator=(DwriteRenderBufferCtxMgr&& r) noexcept {
				m_wrapper = std::move(r.m_wrapper);
				m_pImpl = r.m_pImpl;
				r.m_pImpl = nullptr;
				return *this;
			}

			std::vector<uint8_t>& operator*() const {
				return *m_wrapper;
			}

			~DwriteRenderBufferCtxMgr();
		};

		DwriteRenderBufferCtxMgr AllocateBuffer() const;
	};

#pragma warning(push)
#pragma warning(disable: 4250)
	template<typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class DirectWriteDrawingFont : public DirectWriteFont, public BaseDrawableFont<DestPixFmt, OpacityType> {

		static uint32_t GetEffectiveOpacity(const uint8_t& src) {
			return src;
		}

	public:
		using DirectWriteFont::DirectWriteFont;

		using BaseDrawableFont<DestPixFmt, OpacityType>::Draw;

		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity, OpacityType bgOpacity) const override {
			const auto srcBufCtx = AllocateBuffer();
			auto& srcBuf = *srcBufCtx;
			const auto bbox = DrawCharacter(c, srcBuf, true).Translate(x, y);

			if (!srcBuf.empty()) {
				const auto destWidth = static_cast<SSIZE_T>(to->Width);
				const auto destHeight = static_cast<SSIZE_T>(to->Height);
				const auto srcWidth = bbox.Width();
				const auto srcHeight = bbox.Height();

				GlyphMeasurement src = {false, 0, 0, srcWidth, srcHeight};
				auto dest = bbox;
				src.AdjustToIntersection(dest, srcWidth, srcHeight, destWidth, destHeight);

				if (!src.EffectivelyEmpty() && !dest.EffectivelyEmpty()) {
					auto destBuf = to->View<DestPixFmt>();
					RgbBitmapCopy<uint8_t, GetEffectiveOpacity, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
				}
			}
			return bbox;
		}
	};
#pragma warning(pop)
}
