#pragma once

#include "XivAlexanderCommon/Sqex.h"
#include "XivAlexanderCommon/Sqex/FontCsv/BaseDrawableFont.h"

namespace Sqex::FontCsv {
	class FdtFont : public virtual BaseFont {
		struct Implementation;
		const std::unique_ptr<Implementation> m_pImpl;

	public:
		FdtFont(std::shared_ptr<const ModifiableFontCsvStream> stream, SSIZE_T leftSideBearing);
		~FdtFont() override;

		void SetLeftSideBearing(SSIZE_T leftSideBearing);
		[[nodiscard]] SSIZE_T GetLeftSideBearing() const;

		[[nodiscard]] bool HasCharacter(char32_t) const override;
		[[nodiscard]] float Size() const override;
		[[nodiscard]] const std::vector<char32_t>& GetAllCharacters() const override;
		[[nodiscard]] uint32_t Ascent() const override;
		[[nodiscard]] uint32_t LineHeight() const override;
		[[nodiscard]] const std::map<std::pair<char32_t, char32_t>, SSIZE_T>& GetKerningTable() const override;

		using BaseFont::Measure;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, const FontTableEntry& entry) const;
		[[nodiscard]] GlyphMeasurement Measure(SSIZE_T x, SSIZE_T y, char32_t c) const override;

	protected:
		[[nodiscard]] const ModifiableFontCsvStream& GetStream() const;
	};

	template<typename SrcPixFmt = Texture::RGBA4444, typename DestPixFmt = Texture::RGBA8888, typename OpacityType = uint8_t>
	class FdtDrawableFont : public FdtFont, public BaseDrawableFont<DestPixFmt, OpacityType> {
		std::vector<std::shared_ptr<const Texture::MipmapStream>> m_mipmaps;
		mutable std::vector<std::vector<SrcPixFmt>> m_mipmapBuffers;

		static constexpr uint32_t Scaler = 255;

		template<int ChannelIndex>
		static uint32_t GetEffectiveOpacity(const SrcPixFmt& src) {
			if constexpr (ChannelIndex == 0)
				return Scaler * src.B / SrcPixFmt::MaxB;
			else if constexpr (ChannelIndex == 1)
				return Scaler * src.G / SrcPixFmt::MaxG;
			else if constexpr (ChannelIndex == 2)
				return Scaler * src.R / SrcPixFmt::MaxR;
			else if constexpr (ChannelIndex == 3)
				return Scaler * src.A / SrcPixFmt::MaxA;
			else
				std::abort();  // Cannot reach
		}

		mutable std::mutex m_mipmapBuffersMtx;

	public:
		using BaseDrawableFont<DestPixFmt, OpacityType>::MaxOpacity;

		FdtDrawableFont(std::shared_ptr<const ModifiableFontCsvStream> stream, SSIZE_T leftSideBearing, std::vector<std::shared_ptr<const Texture::MipmapStream>> mipmaps)
			: FdtFont(std::move(stream), leftSideBearing)
			, BaseDrawableFont<DestPixFmt, OpacityType>(this)
			, m_mipmaps(std::move(mipmaps))
			, m_mipmapBuffers(m_mipmaps.size()) {
		}

		~FdtDrawableFont() override = default;

		using BaseDrawableFont<DestPixFmt>::Draw;
		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, const FontTableEntry& entry, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const {
			const auto bbox = Measure(x, y, entry);
			if (to) {
				const auto destWidth = static_cast<SSIZE_T>(to->Width);
				const auto destHeight = static_cast<SSIZE_T>(to->Height);
				const auto srcWidth = static_cast<SSIZE_T>(GetStream().TextureWidth());
				const auto srcHeight = static_cast<SSIZE_T>(GetStream().TextureHeight());

				if (entry.TextureIndex / SrcPixFmt::ChannelCount >= m_mipmaps.size()) {
					throw std::invalid_argument(std::format(
						"Character {} requires font texture #{} channel {}, but only {} textures given",
						ToU8({ entry.Char() }),
						entry.TextureIndex / SrcPixFmt::ChannelCount + 1,
						entry.TextureIndex % SrcPixFmt::ChannelCount,
						m_mipmaps.size()
					));
				}

				auto destBuf = to->View<DestPixFmt>();
				auto& srcBuf = m_mipmapBuffers[entry.TextureIndex / SrcPixFmt::ChannelCount];
				if (srcBuf.empty()) {
					const auto lock = std::lock_guard(m_mipmapBuffersMtx);
					if (srcBuf.empty()) {
						srcBuf = m_mipmaps[entry.TextureIndex / SrcPixFmt::ChannelCount]->template ReadStreamIntoVector<SrcPixFmt>(0);
					}
				}
				const auto channelIndex = entry.TextureIndex % 4;

				GlyphMeasurement src = {
					false,
					entry.TextureOffsetX,
					entry.TextureOffsetY,
					static_cast<SSIZE_T>(0) + entry.TextureOffsetX + entry.BoundingWidth,
					static_cast<SSIZE_T>(0) + entry.TextureOffsetY + entry.BoundingHeight,
				};
				auto dest = bbox;
				src.AdjustToIntersection(dest, srcWidth, srcHeight, destWidth, destHeight);
				if (!src.empty && !dest.empty) {
					if (channelIndex == 0)
						RgbBitmapCopy<SrcPixFmt, GetEffectiveOpacity<0>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, BaseDrawableFont<DestPixFmt, OpacityType>::Gamma());
					else if (channelIndex == 1)
						RgbBitmapCopy<SrcPixFmt, GetEffectiveOpacity<1>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, BaseDrawableFont<DestPixFmt, OpacityType>::Gamma());
					else if (channelIndex == 2)
						RgbBitmapCopy<SrcPixFmt, GetEffectiveOpacity<2>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, BaseDrawableFont<DestPixFmt, OpacityType>::Gamma());
					else if (channelIndex == 3)
						RgbBitmapCopy<SrcPixFmt, GetEffectiveOpacity<3>, DestPixFmt, OpacityType>::CopyTo(src, dest, &srcBuf[0], &destBuf[0], srcWidth, srcHeight, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, BaseDrawableFont<DestPixFmt, OpacityType>::Gamma());
					else
						std::abort();  // Cannot reach
				}
			}
			return bbox;
		}

		GlyphMeasurement Draw(Texture::MemoryBackedMipmap* to, SSIZE_T x, SSIZE_T y, char32_t c, const DestPixFmt& fgColor, const DestPixFmt& bgColor, OpacityType fgOpacity = MaxOpacity, OpacityType bgOpacity = MaxOpacity) const override {
			const auto entry = GetStream().GetFontEntry(c);
			if (!entry)  // skip missing characters
				return {};
			return this->Draw(to, x, y, *entry, fgColor, bgColor, fgOpacity, bgOpacity);
		}

		SSIZE_T FindLeftSideBearing() const {
			const char32_t candidates[] = U"c0";

			const auto srcWidth = static_cast<SSIZE_T>(GetStream().TextureWidth());
			const auto srcHeight = static_cast<SSIZE_T>(GetStream().TextureHeight());
			std::vector<size_t> leftEmptys(255);
			for (const auto& entry : GetStream().GetFontTableEntries()) {
				auto& srcBuf = m_mipmapBuffers[entry.TextureIndex / SrcPixFmt::ChannelCount];
				if (srcBuf.empty()) {
					const auto lock = std::lock_guard(m_mipmapBuffersMtx);
					if (srcBuf.empty()) {
						srcBuf = m_mipmaps[entry.TextureIndex / SrcPixFmt::ChannelCount]->template ReadStreamIntoVector<SrcPixFmt>(0);
					}
				}
				const auto channelIndex = entry.TextureIndex % 4;

				GlyphMeasurement src = {
					false,
					entry.TextureOffsetX,
					entry.TextureOffsetY,
					static_cast<SSIZE_T>(0) + entry.TextureOffsetX + entry.BoundingWidth,
					static_cast<SSIZE_T>(0) + entry.TextureOffsetY + entry.BoundingHeight,
				};

				SSIZE_T res = 0;
				for (auto i = src.left; i < src.right; ++i) {
					bool empty = true;
					for (auto j = src.top; empty && j < src.bottom; ++j) {
						const auto& px = srcBuf[srcWidth * j + i];

						if (channelIndex == 0)
							empty &= !GetEffectiveOpacity<0>(px);
						else if (channelIndex == 1)
							empty &= !GetEffectiveOpacity<1>(px);
						else if (channelIndex == 2)
							empty &= !GetEffectiveOpacity<2>(px);
						else if (channelIndex == 3)
							empty &= !GetEffectiveOpacity<3>(px);
						else
							std::abort();  // Cannot reach
					}
					if (!empty)
						break;
					res = std::max(res, 1 + i - src.left);
				}
				leftEmptys[res]++;
			}
			return std::distance(leftEmptys.begin(), std::ranges::max_element(leftEmptys));
		}
	};
}
