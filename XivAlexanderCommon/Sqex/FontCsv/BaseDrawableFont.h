#pragma once

#include "XivAlexanderCommon/Sqex/FontCsv/BaseFont.h"
#include "XivAlexanderCommon/Sqex/Sqpack.h"
#include "XivAlexanderCommon/Sqex/Texture.h"
#include "XivAlexanderCommon/Sqex/Texture/Mipmap.h"

namespace Sqex::FontCsv {
	template<
		typename SrcPixFmt,
		uint32_t ResolverFunction(const SrcPixFmt&),
		typename DestPixFmt,
		typename OpacityType,
		int VerticalDirection = 1
	> class RgbBitmapCopy {
		static_assert(VerticalDirection == 1 || VerticalDirection == -1);

		static constexpr auto Scaler = 0xFFUL;
		static constexpr auto MaxOpacity = std::numeric_limits<OpacityType>::max();

		static inline void DrawLineToRgb(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, const DestPixFmt& fgColor, const DestPixFmt& bgColor, double gamma) {
			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				const auto blendedBgColor = DestPixFmt{
					(bgColor.R * bgColor.A + destPtr->R * (DestPixFmt::MaxA - bgColor.A)) / DestPixFmt::MaxA,
					(bgColor.G * bgColor.A + destPtr->G * (DestPixFmt::MaxA - bgColor.A)) / DestPixFmt::MaxA,
					(bgColor.B * bgColor.A + destPtr->B * (DestPixFmt::MaxA - bgColor.A)) / DestPixFmt::MaxA,
					DestPixFmt::MaxA - ((DestPixFmt::MaxA - bgColor.A) * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
				};
				const auto blendedFgColor = DestPixFmt{
					(fgColor.R * fgColor.A + destPtr->R * (DestPixFmt::MaxA - fgColor.A)) / DestPixFmt::MaxA,
					(fgColor.G * fgColor.A + destPtr->G * (DestPixFmt::MaxA - fgColor.A)) / DestPixFmt::MaxA,
					(fgColor.B * fgColor.A + destPtr->B * (DestPixFmt::MaxA - fgColor.A)) / DestPixFmt::MaxA,
					DestPixFmt::MaxA - ((DestPixFmt::MaxA - fgColor.A) * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
				};
				const auto currentColor = DestPixFmt{
					(blendedBgColor.R * (Scaler - opacityScaled) + blendedFgColor.R * opacityScaled) / Scaler,
					(blendedBgColor.G * (Scaler - opacityScaled) + blendedFgColor.G * opacityScaled) / Scaler,
					(blendedBgColor.B * (Scaler - opacityScaled) + blendedFgColor.B * opacityScaled) / Scaler,
					(blendedBgColor.A * (Scaler - opacityScaled) + blendedFgColor.A * opacityScaled) / Scaler,
				};
				const auto blendedDestColor = DestPixFmt{
					(destPtr->R * destPtr->A + currentColor.R * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
					(destPtr->G * destPtr->A + currentColor.G * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
					(destPtr->B * destPtr->A + currentColor.B * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
					DestPixFmt::MaxA - ((DestPixFmt::MaxA - destPtr->A) * (DestPixFmt::MaxA - currentColor.A)) / DestPixFmt::MaxA,
				};
				destPtr->R = (blendedDestColor.R * (DestPixFmt::MaxA - currentColor.A) + currentColor.R * currentColor.A) / DestPixFmt::MaxR;
				destPtr->G = (blendedDestColor.G * (DestPixFmt::MaxA - currentColor.A) + currentColor.G * currentColor.A) / DestPixFmt::MaxG;
				destPtr->B = (blendedDestColor.B * (DestPixFmt::MaxA - currentColor.A) + currentColor.B * currentColor.A) / DestPixFmt::MaxB;
				destPtr->A = blendedDestColor.A;
				++destPtr;
				++srcPtr;
			}
		}

		static inline void DrawLineToRgbOpaque(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, const DestPixFmt& fgColor, const DestPixFmt& bgColor, double gamma) {
			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				destPtr->R = (bgColor.R * (Scaler - opacityScaled) + fgColor.R * opacityScaled) / Scaler;
				destPtr->G = (bgColor.G * (Scaler - opacityScaled) + fgColor.G * opacityScaled) / Scaler;
				destPtr->B = (bgColor.B * (Scaler - opacityScaled) + fgColor.B * opacityScaled) / Scaler;
				destPtr->A = DestPixFmt::MaxA;
				++destPtr;
				++srcPtr;
			}
		}

		template<bool ColorIsForeground>
		static inline void DrawLineToRgbBinaryOpacity(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, const DestPixFmt& color, double gamma) {
			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				const auto opacity = DestPixFmt::MaxA * (ColorIsForeground ? opacityScaled : Scaler - opacityScaled) / Scaler;
				if (opacity) {
					const auto blendedDestColor = DestPixFmt{
						(destPtr->R * destPtr->A + color.R * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
						(destPtr->G * destPtr->A + color.G * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
						(destPtr->B * destPtr->A + color.B * (DestPixFmt::MaxA - destPtr->A)) / DestPixFmt::MaxA,
						DestPixFmt::MaxA - ((DestPixFmt::MaxA - destPtr->A) * (DestPixFmt::MaxA - opacity)) / DestPixFmt::MaxA,
					};
					destPtr->R = (blendedDestColor.R * (DestPixFmt::MaxA - opacity) + color.R * opacity) / DestPixFmt::MaxR;
					destPtr->G = (blendedDestColor.G * (DestPixFmt::MaxA - opacity) + color.G * opacity) / DestPixFmt::MaxG;
					destPtr->B = (blendedDestColor.B * (DestPixFmt::MaxA - opacity) + color.B * opacity) / DestPixFmt::MaxB;
					destPtr->A = blendedDestColor.A;
				}
				++destPtr;
				++srcPtr;
			}
		}

		static inline void DrawLineToL8(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity, OpacityType bgOpacity, double gamma) {
			constexpr auto DestPixFmtMax = std::numeric_limits<DestPixFmt>::max();

			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				const auto blendedBgColor = (1 * bgColor * bgOpacity + 1 * *destPtr * (MaxOpacity - bgOpacity)) / MaxOpacity;
				const auto blendedFgColor = (1 * fgColor * fgOpacity + 1 * *destPtr * (MaxOpacity - fgOpacity)) / MaxOpacity;
				*destPtr = static_cast<DestPixFmt>((blendedBgColor * (Scaler - opacityScaled) + blendedFgColor * opacityScaled) / Scaler);
				++destPtr;
				++srcPtr;
			}
		}

		static inline void DrawLineToL8Opaque(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, double gamma) {
			constexpr auto DestPixFmtMax = std::numeric_limits<DestPixFmt>::max();

			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				*destPtr = static_cast<DestPixFmt>(MaxOpacity * opacityScaled / Scaler);
				++destPtr;
				++srcPtr;
			}
		}

		template<bool ColorIsForeground>
		static inline void DrawLineToL8BinaryOpacity(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, const DestPixFmt& color, double gamma) {
			constexpr auto DestPixFmtMax = std::numeric_limits<DestPixFmt>::max();

			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				const auto opacityScaled2 = ColorIsForeground ? opacityScaled : Scaler - opacityScaled;
				*destPtr = static_cast<DestPixFmt>((*destPtr * (Scaler - opacityScaled2) + 1 * color * opacityScaled2) / Scaler);
				++destPtr;
				++srcPtr;
			}
		}

	public:
		static inline void CopyTo(const GlyphMeasurement& src, const GlyphMeasurement& dest, const SrcPixFmt* srcBuf, DestPixFmt* destBuf, SSIZE_T srcWidth, SSIZE_T srcHeight, SSIZE_T destWidth, DestPixFmt fgColor, DestPixFmt bgColor, OpacityType fgOpacity, OpacityType bgOpacity, double gamma) {
			auto destPtrBegin = &destBuf[static_cast<size_t>(1) * dest.top * destWidth + dest.left];
			auto srcPtrBegin = &srcBuf[static_cast<size_t>(1) * (VerticalDirection == 1 ? src.top : srcHeight - src.top - 1) * srcWidth + src.left];
			const auto srcPtrDelta = srcWidth * VerticalDirection;
			const auto regionWidth = src.right - src.left;
			const auto regionHeight = src.bottom - src.top;

			gamma = 1.0 / gamma;

			if constexpr (std::is_integral_v<DestPixFmt>) {
				constexpr auto DestPixFmtMax = std::numeric_limits<DestPixFmt>::max();

				if (fgOpacity == MaxOpacity && bgOpacity == MaxOpacity && fgColor == DestPixFmtMax && bgColor == 0) {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToL8Opaque(destPtrBegin, srcPtrBegin, regionWidth, gamma);
				} else if (fgOpacity == MaxOpacity && bgOpacity == 0) {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToL8BinaryOpacity<true>(destPtrBegin, srcPtrBegin, regionWidth, fgColor, gamma);
				} else if (fgOpacity == 0 && bgOpacity == MaxOpacity) {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToL8BinaryOpacity<false>(destPtrBegin, srcPtrBegin, regionWidth, bgColor, gamma);
				} else {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToL8(destPtrBegin, srcPtrBegin, regionWidth, fgColor, bgColor, fgOpacity, bgOpacity, gamma);
				}
			} else {
				fgColor.A = fgColor.A * fgOpacity / std::numeric_limits<OpacityType>::max();
				bgColor.A = bgColor.A * bgOpacity / std::numeric_limits<OpacityType>::max();
				if (fgColor.A == DestPixFmt::MaxA && bgColor.A == DestPixFmt::MaxA) {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToRgbOpaque(destPtrBegin, srcPtrBegin, regionWidth, fgColor, bgColor, gamma);
				} else if (fgColor.A == DestPixFmt::MaxA && bgColor.A == 0) {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToRgbBinaryOpacity<true>(destPtrBegin, srcPtrBegin, regionWidth, fgColor, gamma);
				} else if (fgColor.A == 0 && bgColor.A == DestPixFmt::MaxA) {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToRgbBinaryOpacity<false>(destPtrBegin, srcPtrBegin, regionWidth, bgColor, gamma);
				} else {
					for (auto i = 0; i < regionHeight; ++i, destPtrBegin += destWidth, srcPtrBegin += srcPtrDelta)
						DrawLineToRgb(destPtrBegin, srcPtrBegin, regionWidth, fgColor, bgColor, gamma);
				}
			}
		}

	};

	template<typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class BaseDrawableFont {
	public:
		static constexpr auto MaxOpacity = std::numeric_limits<OpacityType>::max();

	protected:
		double m_gamma = 1.0;

	public:
		BaseFont& Base;

		BaseDrawableFont(BaseFont* font)
			: Base(*font) {
		}

		void Gamma(double gamma) { m_gamma = gamma; }

		double Gamma() const { return m_gamma; }

		virtual GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const = 0;

		virtual GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, const std::u32string& s, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const {
			if (s.empty())
				return {};

			char32_t lastChar = 0;
			const auto iHeight = static_cast<SSIZE_T>(Base.LineHeight());

			GlyphMeasurement result{};
			SSIZE_T currX = x, currY = y;

			for (const auto currChar : s) {
				if (currChar == u'\r') {
					continue;
				} else if (currChar == u'\n') {
					currX = x;
					currY += iHeight;
					lastChar = 0;
					continue;
				} else if (currChar == u'\u200c') {  // unicode non-joiner
					lastChar = 0;
					continue;
				}

				const auto kerning = Base.GetKerning(lastChar, currChar);
				const auto currBbox = Draw(to, currX + kerning, currY, currChar, fgColor, bgColor, fgOpacity, bgOpacity);
				currX += kerning + currBbox.advanceX;
				result.ExpandToFit(currBbox);
				lastChar = currChar;
			}
			if (result.empty)
				return { true };
			
			return result;
		}

		virtual GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, const std::string& s, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const {
			return Draw(to, x, y, ToU32(s), fgColor, bgColor, fgOpacity, bgOpacity);
		}
	};

	template<typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class CascadingDrawableFont : public CascadingFont, public BaseDrawableFont<DestPixFmt, OpacityType> {

		static std::vector<std::shared_ptr<BaseFont>> ConvertVector(std::vector<std::shared_ptr<BaseDrawableFont<DestPixFmt>>> in) {
			std::vector<std::shared_ptr<BaseFont>> res;
			for (auto& item : in)
				res.emplace_back(std::static_pointer_cast<BaseFont>(std::move(item)));
			return res;
		}

	public:
		using BaseDrawableFont<DestPixFmt, OpacityType>::MaxOpacity;

		CascadingDrawableFont(std::vector<std::shared_ptr<BaseDrawableFont<DestPixFmt>>> fontList)
			: CascadingFont(ConvertVector(std::move(fontList)))
			, BaseDrawableFont<DestPixFmt, OpacityType>(this) {
		}

		CascadingDrawableFont(std::vector<std::shared_ptr<BaseDrawableFont<DestPixFmt>>> fontList, float normalizedSize, uint32_t ascent, uint32_t lineHeight)
			: CascadingFont(ConvertVector(std::move(fontList)), normalizedSize, ascent, lineHeight)
			, BaseDrawableFont<DestPixFmt, OpacityType>(this) {
		}

		~CascadingDrawableFont() override = default;

		using BaseDrawableFont<DestPixFmt>::Draw;

		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const override {
			for (const auto& f : GetFontList()) {
				const auto df = dynamic_cast<BaseDrawableFont<DestPixFmt>*>(f.get());
				const auto currBbox = df->Draw(to, x, y + Ascent() - df->Ascent(), c, fgColor, bgColor, fgOpacity, bgOpacity);
				if (!currBbox.empty)
					return currBbox;
			}
			return { true };
		}
	};

	template<typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class OversampledFont : public BaseFont, public BaseDrawableFont<DestPixFmt, OpacityType> {
		const std::shared_ptr<BaseDrawableFont<DestPixFmt, OpacityType>> m_underlying;
		const int m_scale;

		mutable std::optional<std::map<std::pair<char32_t, char32_t>, SSIZE_T>> m_kerningTable;

		static uint32_t ResolverFunction(const uint8_t& n) {
			return n;
		}

	public:
		using BaseDrawableFont<DestPixFmt, OpacityType>::MaxOpacity;

		OversampledFont(std::shared_ptr<BaseDrawableFont<DestPixFmt, OpacityType>> underlying, int scale)
			: BaseFont()
			, BaseDrawableFont<DestPixFmt, OpacityType>(this)
			, m_underlying(std::move(underlying))
			, m_scale(scale) {
		}

		bool HasCharacter(char32_t c) const override {
			return m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.HasCharacter(c);
		}

		float Size() const override {
			return (m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.Size() + m_scale - 1) / m_scale;
		}

		const std::vector<char32_t>& GetAllCharacters() const override {
			return m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.GetAllCharacters();
		}

		uint32_t Ascent() const override {
			return (m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.Ascent() + m_scale - 1) / m_scale;
		}

		uint32_t LineHeight() const override {
			return (m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.LineHeight() + m_scale - 1) / m_scale;
		}

		const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override {
			if (!m_kerningTable) {
				m_kerningTable = std::map<std::pair<char32_t, char32_t>, SSIZE_T>{};
				for (const auto& [k, v] : m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.GetKerningTable()) {
					if (v / m_scale)
						m_kerningTable->insert_or_assign(k, v / m_scale);
				}
			}
			return *m_kerningTable;
		}

		SSIZE_T GetKerning(char32_t l, char32_t r, SSIZE_T defaultOffset = 0) const override {
			const auto& kt = GetKerningTable();
			if (const auto it = kt.find(std::make_pair(l, r)); it == kt.end())
				return defaultOffset;
			else
				return it->second;
		}

		GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override {
			auto size2 = m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.Measure(0, 0, c);
			size2.left /= m_scale;
			size2.top /= m_scale;
			size2.right = (size2.right + m_scale - 1) / m_scale;
			size2.bottom = (size2.bottom + m_scale - 1) / m_scale;
			size2.advanceX /= m_scale;
			size2.Translate(x, y);
			return size2;
		}

		using BaseDrawableFont<DestPixFmt, OpacityType>::Draw;

		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const override {
			auto size1 = m_underlying->BaseDrawableFont<DestPixFmt, OpacityType>::Base.Measure(0, 0, c);
			auto size2 = GlyphMeasurement(size1);
			size2.left /= m_scale;
			size2.top /= m_scale;
			size2.right = size1.left == size2.right ? size2.left : (size2.right + m_scale - 1) / m_scale;
			size2.bottom = size1.top == size2.bottom ? size2.top : (size2.bottom + m_scale - 1) / m_scale;
			size2.advanceX /= m_scale;
			if (size2.EffectivelyEmpty())
				return size2;

			const auto tex1baseleft = size1.left > 0 ? 0 : -size1.left;
			const auto tex1basetop = size1.top > 0 ? 0 : -size1.top;
			Texture::MemoryBackedMipmap tex1(static_cast<uint16_t>(tex1baseleft + size1.right), static_cast<uint16_t>(tex1basetop + size1.bottom), 1, Texture::Format::L8);
			auto buf1 = tex1.View<uint8_t>();
			m_underlying->Draw(&tex1, tex1baseleft, tex1basetop, c, 0xFF, 0x00);

			const auto tex2baseleft = size2.left > 0 ? 0 : -size2.left;
			const auto tex2basetop = size2.top > 0 ? 0 : -size2.top;
			Texture::MemoryBackedMipmap tex2(static_cast<uint16_t>(tex2baseleft + size2.right), static_cast<uint16_t>(tex2basetop + size2.bottom), 1, Texture::Format::L8);
			auto buf2 = tex2.View<uint8_t>();
			for (SSIZE_T i = 0; i < tex2.Height; ++i) {
				for (SSIZE_T j = 0; j < tex2.Width; ++j) {
					int accumulator = 0, cnt = 0;
					for (SSIZE_T i2 = i * m_scale, i2_ = std::min<SSIZE_T>(i2 + m_scale, tex1.Height); i2 < i2_; i2++)
						for (SSIZE_T j2 = j * m_scale, j2_ = std::min<SSIZE_T>(j2 + m_scale, tex1.Width); j2 < j2_; j2++) {
							accumulator += buf1[i2 * tex1.Width + j2];
							cnt++;
						}
					buf2[i * tex2.Width + j] = cnt ? static_cast<uint8_t>(accumulator / cnt) : 0;
				}
			}

			auto destSize = GlyphMeasurement(size2);
			destSize.Translate(x, y);
			auto destBuf = to->View<DestPixFmt>();

			RgbBitmapCopy<uint8_t, ResolverFunction, DestPixFmt, OpacityType>::CopyTo(size2, destSize, &buf2[0], &destBuf[0], tex2.Width, tex2.Height, to->Width, fgColor, bgColor, fgOpacity, bgOpacity, BaseDrawableFont<DestPixFmt, OpacityType>::Gamma());
			return destSize;
		}
	};
}
