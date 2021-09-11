#pragma once

#include "Sqex_FontCsv_SeCompatibleDrawableFont.h"

namespace Sqex::FontCsv {
	class GdiFont : public virtual SeCompatibleFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		GdiFont(const LOGFONTW&);
		~GdiFont() override;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] SSIZE_T GetCharacterWidth(char32_t c) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t Descent() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using SeCompatibleFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		class DeviceContextWrapper {
			const HDC m_hdc;
			const Utils::CallOnDestruction m_hdcRelease;

			const HFONT m_hFont;
			const Utils::CallOnDestruction m_fontRelease;
			const HFONT m_hPrevFont;
			const Utils::CallOnDestruction m_prevFontRevert;

			std::vector<uint8_t> m_readBuffer;

		public:
			const TEXTMETRICW Metrics;
			DeviceContextWrapper(const LOGFONTW& logfont);
			~DeviceContextWrapper();

			[[nodiscard]] HDC GetDC() const { return m_hdc; }
			[[nodiscard]] SSIZE_T GetCharacterWidth(char32_t c) const;
			[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const;

			std::pair<const std::vector<uint8_t>*, GlyphMeasurement> Draw(SSIZE_T x, SSIZE_T y, char32_t c);
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
	class GdiDrawingFont : public GdiFont, public SeCompatibleDrawableFont<DestPixFmt, OpacityType> {

		static uint32_t GetEffectiveOpacity(const uint8_t& src) {
			return src * 255 / 65;
		}

		DeviceContextWrapperContext buffer;

	public:
		using GdiFont::GdiFont;

		using SeCompatibleDrawableFont<DestPixFmt, OpacityType>::Draw;
		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity, OpacityType bgOpacity) const override {
			if (!HasCharacter(c))
				return { true };

			const auto buffer = AllocateDeviceContext();
			const auto [pSrcBuf, bbox] = buffer->Draw(x, y, c);
			const auto& srcBuf = *pSrcBuf;
			if (srcBuf.empty())
				return { true };

			const auto destWidth = static_cast<SSIZE_T>(to->Width());
			const auto destHeight = static_cast<SSIZE_T>(to->Height());
			const auto srcWidth = Sqpack::Align<SSIZE_T>(bbox.Width(), 4).Alloc;
			const auto srcHeight = bbox.Height();

			GlyphMeasurement src = { false, 0, 0, srcWidth, srcHeight };
			auto dest = bbox; /* GlyphMeasurement{
				false,
				x,
				bbox.top,
				x + bbox.right - bbox.left,
				bbox.bottom,
			};*/
			src.AdjustToIntersection(dest, srcWidth, srcHeight, destWidth, destHeight);

			if (!src.empty && !dest.empty) {
				auto destBuf = to->View<DestPixFmt>();
				RgbBitmapCopy<uint8_t, GetEffectiveOpacity, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity);
			}
			return bbox;
		}
	};
#pragma warning(pop)
}
