#ifndef _XIVRES_INTERNAL_BitmapCopy_H_
#define _XIVRES_INTERNAL_BitmapCopy_H_

#include <array>
#include <cmath>

#include "XivRes/PixelFormats.h"

namespace XivRes::Internal {
	enum class BitmapVerticalDirection : int {
		TopRowFirst = 1,
		Undefined = 0,
		BottomRowFirst = -1,
	};

	namespace BitmapCopy {
		static std::vector<uint8_t> CreateGammaTable(float gamma) {
			std::vector<uint8_t> res(256);
			for (int i = 0; i < 256; i++)
				res[i] = static_cast<uint8_t>(std::powf(static_cast<float>(i) / 255.f, 1 / gamma) * 255.f);
			return res;
		}

		class ToRGBA8888 {
			std::span<const uint8_t> m_gammaTable;

			RGBA8888 m_colorForeground = RGBA8888(0, 0, 0, 255);
			RGBA8888 m_colorBackground = RGBA8888(0, 0, 0, 0);

			const uint8_t* m_pSource = nullptr;
			size_t m_nSourceWidth = 0;
			size_t m_nSourceHeight = 0;
			size_t m_nSourceStride = 0;
			BitmapVerticalDirection m_nSourceVerticalDirection = BitmapVerticalDirection::Undefined;

			RGBA8888* m_pTarget = nullptr;
			size_t m_nTargetWidth = 0;
			size_t m_nTargetHeight = 0;
			BitmapVerticalDirection m_nTargetVerticalDirection = BitmapVerticalDirection::Undefined;

		public:
			ToRGBA8888& From(const void* pBuf, size_t width, size_t height, size_t stride, BitmapVerticalDirection verticalDirection) {
				m_pSource = static_cast<const uint8_t*>(pBuf);
				m_nSourceWidth = width;
				m_nSourceHeight = height;
				m_nSourceStride = stride;
				m_nSourceVerticalDirection = verticalDirection;
				return *this;
			}

			ToRGBA8888& To(RGBA8888* pBuf, size_t width, size_t height, BitmapVerticalDirection verticalDirection) {
				m_pTarget = pBuf;
				m_nTargetWidth = width;
				m_nTargetHeight = height;
				m_nTargetVerticalDirection = verticalDirection;
				return *this;
			}

			ToRGBA8888& WithGammaTable(std::span<const uint8_t> gammaTable) {
				m_gammaTable = gammaTable;
				return *this;
			}

			ToRGBA8888& WithForegroundColor(RGBA8888 color) {
				m_colorForeground = color;
				return *this;
			}

			ToRGBA8888& WithBackgroundColor(RGBA8888 color) {
				m_colorBackground = color;
				return *this;
			}

			void CopyTo(int srcX1, int srcY1, int srcX2, int srcY2, int targetX1, int targetY1) {
				auto destPtrBegin = &m_pTarget[(m_nTargetVerticalDirection == BitmapVerticalDirection::TopRowFirst ? targetY1 : m_nTargetHeight - targetY1 - 1) * m_nTargetWidth + targetX1];
				const auto destPtrDelta = m_nTargetWidth * static_cast<int>(m_nTargetVerticalDirection);

				auto srcPtrBegin = &m_pSource[m_nSourceStride * ((m_nSourceVerticalDirection == BitmapVerticalDirection::TopRowFirst ? srcY1 : m_nSourceHeight - srcY1 - 1) * m_nSourceWidth + srcX1)];
				const auto srcPtrDelta = m_nSourceStride * m_nSourceWidth * static_cast<int>(m_nSourceVerticalDirection);

				const auto nCols = srcX2 - srcX1;
				auto nRemainingRows = srcY2 - srcY1;

				if (m_colorForeground.A == 255 && m_colorBackground.A == 255) {
					while (nRemainingRows--) {
						DrawLineToRgbOpaque(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}

				} else if (m_colorForeground.A == 255 && m_colorBackground.A == 0) {
					while (nRemainingRows--) {
						DrawLineToRgbBinaryOpacity<true>(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}

				} else if (m_colorForeground.A == 0 && m_colorBackground.A == 255) {
					while (nRemainingRows--) {
						DrawLineToRgbBinaryOpacity<false>(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}

				} else if (m_colorForeground.A == 0 && m_colorBackground.A == 0) {
					return;

				} else {
					while (nRemainingRows--) {
						DrawLineToRgb(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}
				}
			}

		private:
			void DrawLineToRgb(RGBA8888* pTarget, const uint8_t* pSource, size_t nPixelCount) {
				while (nPixelCount--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					const auto blendedBgColor = RGBA8888{
						(m_colorBackground.R * m_colorBackground.A + pTarget->R * (255 - m_colorBackground.A)) / 255,
						(m_colorBackground.G * m_colorBackground.A + pTarget->G * (255 - m_colorBackground.A)) / 255,
						(m_colorBackground.B * m_colorBackground.A + pTarget->B * (255 - m_colorBackground.A)) / 255,
						255 - ((255 - m_colorBackground.A) * (255 - pTarget->A)) / 255,
					};
					const auto blendedFgColor = RGBA8888{
						(m_colorForeground.R * m_colorForeground.A + pTarget->R * (255 - m_colorForeground.A)) / 255,
						(m_colorForeground.G * m_colorForeground.A + pTarget->G * (255 - m_colorForeground.A)) / 255,
						(m_colorForeground.B * m_colorForeground.A + pTarget->B * (255 - m_colorForeground.A)) / 255,
						255 - ((255 - m_colorForeground.A) * (255 - pTarget->A)) / 255,
					};
					const auto currentColor = RGBA8888{
						(blendedBgColor.R * (255 - opacityScaled) + blendedFgColor.R * opacityScaled) / 255,
						(blendedBgColor.G * (255 - opacityScaled) + blendedFgColor.G * opacityScaled) / 255,
						(blendedBgColor.B * (255 - opacityScaled) + blendedFgColor.B * opacityScaled) / 255,
						(blendedBgColor.A * (255 - opacityScaled) + blendedFgColor.A * opacityScaled) / 255,
					};
					const auto blendedDestColor = RGBA8888{
						(pTarget->R * pTarget->A + currentColor.R * (255 - pTarget->A)) / 255,
						(pTarget->G * pTarget->A + currentColor.G * (255 - pTarget->A)) / 255,
						(pTarget->B * pTarget->A + currentColor.B * (255 - pTarget->A)) / 255,
						255 - ((255 - pTarget->A) * (255 - currentColor.A)) / 255,
					};
					pTarget->R = (blendedDestColor.R * (255 - currentColor.A) + currentColor.R * currentColor.A) / 255;
					pTarget->G = (blendedDestColor.G * (255 - currentColor.A) + currentColor.G * currentColor.A) / 255;
					pTarget->B = (blendedDestColor.B * (255 - currentColor.A) + currentColor.B * currentColor.A) / 255;
					pTarget->A = blendedDestColor.A;
					++pTarget;
					pSource += m_nSourceStride;
				}
			}

			void DrawLineToRgbOpaque(RGBA8888* pTarget, const uint8_t* pSource, size_t nPixelCount) {
				while (nPixelCount--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					pTarget->R = (m_colorBackground.R * (255 - opacityScaled) + m_colorForeground.R * opacityScaled) / 255;
					pTarget->G = (m_colorBackground.G * (255 - opacityScaled) + m_colorForeground.G * opacityScaled) / 255;
					pTarget->B = (m_colorBackground.B * (255 - opacityScaled) + m_colorForeground.B * opacityScaled) / 255;
					pTarget->A = 255;
					++pTarget;
					pSource += m_nSourceStride;
				}
			}

			template<bool ColorIsForeground>
			void DrawLineToRgbBinaryOpacity(RGBA8888* pTarget, const uint8_t* pSource, size_t nPixelCount) {
				const auto color = ColorIsForeground ? m_colorForeground : m_colorBackground;
				while (nPixelCount--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					const auto opacity = 255 * (ColorIsForeground ? opacityScaled : 255 - opacityScaled) / 255;
					if (opacity) {
						const auto blendedDestColor = RGBA8888{
							(pTarget->R * pTarget->A + color.R * (255 - pTarget->A)) / 255,
							(pTarget->G * pTarget->A + color.G * (255 - pTarget->A)) / 255,
							(pTarget->B * pTarget->A + color.B * (255 - pTarget->A)) / 255,
							255 - ((255 - pTarget->A) * (255 - opacity)) / 255,
						};
						pTarget->R = (blendedDestColor.R * (255 - opacity) + color.R * opacity) / 255;
						pTarget->G = (blendedDestColor.G * (255 - opacity) + color.G * opacity) / 255;
						pTarget->B = (blendedDestColor.B * (255 - opacity) + color.B * opacity) / 255;
						pTarget->A = blendedDestColor.A;
					}
					++pTarget;
					pSource += m_nSourceStride;
				}
			}
		};

		class ToL8 {
			std::span<const uint8_t> m_gammaTable;

			uint8_t m_colorForeground = 255;
			uint8_t m_colorBackground = 0;
			uint8_t m_opacityForeground = 255;
			uint8_t m_opacityBackground = 0;

			const uint8_t* m_pSource = nullptr;
			size_t m_nSourceWidth = 0;
			size_t m_nSourceHeight = 0;
			size_t m_nSourceStride = 0;
			BitmapVerticalDirection m_nSourceVerticalDirection = BitmapVerticalDirection::Undefined;

			uint8_t* m_pTarget = nullptr;
			size_t m_nTargetWidth = 0;
			size_t m_nTargetHeight = 0;
			size_t m_nTargetStride = 0;
			BitmapVerticalDirection m_nTargetVerticalDirection = BitmapVerticalDirection::Undefined;

		public:
			ToL8& From(const void* pBuf, size_t width, size_t height, size_t stride, BitmapVerticalDirection verticalDirection) {
				m_pSource = static_cast<const uint8_t*>(pBuf);
				m_nSourceWidth = width;
				m_nSourceHeight = height;
				m_nSourceStride = stride;
				m_nSourceVerticalDirection = verticalDirection;
				return *this;
			}

			ToL8& To(uint8_t* pBuf, size_t width, size_t height, size_t stride, BitmapVerticalDirection verticalDirection) {
				m_pTarget = pBuf;
				m_nTargetWidth = width;
				m_nTargetHeight = height;
				m_nTargetStride = stride;
				m_nTargetVerticalDirection = verticalDirection;
				return *this;
			}

			ToL8& WithGammaTable(std::span<const uint8_t> gammaTable) {
				m_gammaTable = gammaTable;
				return *this;
			}

			ToL8& WithForegroundColor(uint8_t color) {
				m_colorForeground = color;
				return *this;
			}

			ToL8& WithBackgroundColor(uint8_t color) {
				m_colorBackground = color;
				return *this;
			}

			ToL8& WithForegroundOpacity(uint8_t opacity) {
				m_opacityForeground = opacity;
				return *this;
			}

			ToL8& WithBackgroundOpacity(uint8_t opacity) {
				m_opacityBackground = opacity;
				return *this;
			}

			void CopyTo(int srcX1, int srcY1, int srcX2, int srcY2, int targetX1, int targetY1) {
				auto destPtrBegin = &m_pTarget[m_nTargetStride * ((m_nTargetVerticalDirection == BitmapVerticalDirection::TopRowFirst ? targetY1 : m_nTargetHeight - targetY1 - 1) * m_nTargetWidth + targetX1)];
				const auto destPtrDelta = m_nTargetStride * m_nTargetWidth * static_cast<int>(m_nTargetVerticalDirection);

				auto srcPtrBegin = &m_pSource[m_nSourceStride * ((m_nSourceVerticalDirection == BitmapVerticalDirection::TopRowFirst ? srcY1 : m_nSourceHeight - srcY1 - 1) * m_nSourceWidth + srcX1)];
				const auto srcPtrDelta = m_nSourceStride * m_nSourceWidth * static_cast<int>(m_nSourceVerticalDirection);

				const auto nCols = srcX2 - srcX1;
				auto nRemainingRows = srcY2 - srcY1;

				if (m_opacityForeground == 255 && m_opacityBackground == 255) {
					while (nRemainingRows--) {
						DrawLineToL8Opaque(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}

				} else if (m_opacityForeground == 255 && m_opacityBackground == 0) {
					while (nRemainingRows--) {
						DrawLineToL8BinaryOpacity<true>(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}

				} else if (m_opacityForeground == 0 && m_opacityBackground == 255) {
					while (nRemainingRows--) {
						DrawLineToL8BinaryOpacity<false>(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}

				} else if (m_opacityForeground == 0 && m_opacityBackground == 0) {
					return;

				} else {
					while (nRemainingRows--) {
						DrawLineToL8(destPtrBegin, srcPtrBegin, nCols);
						destPtrBegin += destPtrDelta;
						srcPtrBegin += srcPtrDelta;
					}
				}
			}

		private:
			void DrawLineToL8(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) {
				while (regionWidth--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					const auto blendedBgColor = (1 * m_colorBackground * m_opacityBackground + 1 * *pTarget * (255 - m_opacityBackground)) / 255;
					const auto blendedFgColor = (1 * m_colorForeground * m_opacityForeground + 1 * *pTarget * (255 - m_opacityForeground)) / 255;
					*pTarget = static_cast<uint8_t>((blendedBgColor * (255 - opacityScaled) + blendedFgColor * opacityScaled) / 255);
					pTarget += m_nTargetStride;
					pSource += m_nSourceStride;
				}
			}

			void DrawLineToL8Opaque(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) {
				while (regionWidth--) {
					*pTarget = m_gammaTable[*pSource];
					pTarget += m_nTargetStride;
					pSource += m_nSourceStride;
				}
			}

			template<bool ColorIsForeground>
			void DrawLineToL8BinaryOpacity(uint8_t* pTarget, const uint8_t* pSource, size_t regionWidth) {
				const auto color = ColorIsForeground ? m_colorForeground : m_colorBackground;
				while (regionWidth--) {
					const auto opacityScaled = m_gammaTable[*pSource];
					const auto opacityScaled2 = ColorIsForeground ? opacityScaled : 255 - opacityScaled;
					*pTarget = static_cast<uint8_t>((*pTarget * (255 - opacityScaled2) + 1 * color * opacityScaled2) / 255);
					pTarget += m_nTargetStride;
					pSource += m_nSourceStride;
				}
			}
		};
	};
}

#endif
