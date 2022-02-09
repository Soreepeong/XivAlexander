#pragma once

#include "XivAlexanderCommon/Sqex/FontCsv/BaseDrawableFont.h"

namespace Sqex::FontCsv {
	class GdiFont : public virtual BaseFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		GdiFont(const LOGFONTW&);
		~GdiFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t LineHeight() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using BaseFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		class DeviceContextWrapper {
			const GdiFont* const m_owner;
			const HDC m_hdc;
			const CallOnDestruction m_hdcRelease;

			const HFONT m_hFont;
			const CallOnDestruction m_fontRelease;
			const HFONT m_hPrevFont;
			const CallOnDestruction m_prevFontRevert;
			
			HBITMAP m_hBitmap = nullptr;
			CallOnDestruction m_bitmapRelease;
			HBITMAP m_hPrevBitmap = nullptr;
			CallOnDestruction m_prevBitmapRevert;
			struct BitmapInfoWithColorSpecContainer {
				BITMAPINFO bmi;
				DWORD dummy[2];
			} m_bmi{};

			std::vector<Texture::RGBA8888> m_readBuffer;

		public:
			const TEXTMETRICW Metrics;
			DeviceContextWrapper(const GdiFont* owner, const LOGFONTW& logfont);
			~DeviceContextWrapper();

			[[nodiscard]] HDC GetDC() const { return m_hdc; }
			[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const;

			struct DrawResult {
				const std::vector<Texture::RGBA8888>* Buffer = nullptr;
				GlyphMeasurement Measurement;
				SSIZE_T BufferWidth = 0;
				SSIZE_T BufferHeight = 0;
			};
			DrawResult Draw(SSIZE_T x, SSIZE_T y, char32_t c);
		};

		class DeviceContextWrapperContext {
			const GdiFont* m_font;
			std::unique_ptr<DeviceContextWrapper> m_wrapper;

		public:
			DeviceContextWrapperContext() :
				m_font(nullptr), m_wrapper(nullptr) {
			}
			DeviceContextWrapperContext(const GdiFont* font, std::unique_ptr<DeviceContextWrapper> wrapper)
				: m_font(font)
				, m_wrapper(std::move(wrapper)) {
			}
			DeviceContextWrapperContext(const DeviceContextWrapperContext&) = delete;
			DeviceContextWrapperContext(DeviceContextWrapperContext&& r) noexcept
				: m_font(r.m_font)
				, m_wrapper(std::move(r.m_wrapper)) {
				r.m_font = nullptr;
			}
			DeviceContextWrapperContext& operator=(const DeviceContextWrapperContext&) = delete;
			DeviceContextWrapperContext& operator=(DeviceContextWrapperContext&& r) noexcept {
				m_font = r.m_font;
				m_wrapper = std::move(r.m_wrapper);
				r.m_font = nullptr;
				return *this;
			}

			DeviceContextWrapper* operator->() const {
				return m_wrapper.get();
			}

			~DeviceContextWrapperContext() {
				if (m_wrapper)
					m_font->FreeDeviceContext(std::move(m_wrapper));
			}
		};

		DeviceContextWrapperContext AllocateDeviceContext() const;
		void FreeDeviceContext(std::unique_ptr<DeviceContextWrapper> wrapper) const;
	};

#pragma warning(push)
#pragma warning(disable: 4250)
	template<typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class GdiDrawingFont : public GdiFont, public BaseDrawableFont<DestPixFmt, OpacityType> {

		static uint32_t GetEffectiveOpacity(const Texture::RGBA8888& src) {
			return src.R;
		}

		DeviceContextWrapperContext buffer;

	public:
		using GdiFont::GdiFont;

		using BaseDrawableFont<DestPixFmt, OpacityType>::Draw;
		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity, OpacityType bgOpacity) const override {
			if (!HasCharacter(c))
				return { true };

			const auto buffer = AllocateDeviceContext();
			const auto [pSrcBuf, bbox, srcWidth, srcHeight] = buffer->Draw(x, y, c);
			const auto& srcBuf = *pSrcBuf;
			if (srcBuf.empty())
				return { true };

			const auto destWidth = static_cast<SSIZE_T>(to->Width);
			const auto destHeight = static_cast<SSIZE_T>(to->Height);

			GlyphMeasurement src = { false, 0, 0, bbox.Width(), bbox.Height() };
			auto dest = bbox;
			src.AdjustToIntersection(dest, srcWidth, srcHeight, destWidth, destHeight);

			if (!src.empty && !dest.empty) {
				auto destBuf = to->View<DestPixFmt>();
				RgbBitmapCopy<Texture::RGBA8888, GetEffectiveOpacity, DestPixFmt, OpacityType, -1>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
			}
			return bbox;
		}
	};
#pragma warning(pop)
}
