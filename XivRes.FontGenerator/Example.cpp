#include <iostream>
#include <Windows.h>
#include <windowsx.h>

#include "XivRes/GameReader.h"
#include "XivRes/FontdataStream.h"
#include "XivRes/MipmapStream.h"
#include "XivRes/PackedFileUnpackingStream.h"
#include "XivRes/PixelFormats.h"
#include "XivRes/TextureStream.h"
#include "XivRes/internal/CallOnDestruction.h"

namespace XivRes::FontGenerator {
	struct GlyphMetrics {
		using MetricType = int;

		bool Empty = true;
		MetricType X1 = 0;
		MetricType Y1 = 0;
		MetricType X2 = 0;
		MetricType Y2 = 0;
		MetricType AdvanceX = 0;

		void AdjustToIntersection(GlyphMetrics& r, MetricType srcWidth, MetricType srcHeight, MetricType destWidth, MetricType destHeight) {
			if (X1 < 0) {
				r.X1 -= X1;
				X1 = 0;
			}
			if (r.X1 < 0) {
				X1 -= r.X1;
				r.X1 = 0;
			}
			if (Y1 < 0) {
				r.Y1 -= Y1;
				Y1 = 0;
			}
			if (r.Y1 < 0) {
				Y1 -= r.Y1;
				r.Y1 = 0;
			}
			if (X2 >= srcWidth) {
				r.X2 -= X2 - srcWidth;
				X2 = srcWidth;
			}
			if (r.X2 >= destWidth) {
				X2 -= r.X2 - destWidth;
				r.X2 = destWidth;
			}
			if (Y2 >= srcHeight) {
				r.Y2 -= Y2 - srcHeight;
				Y2 = srcHeight;
			}
			if (r.Y2 >= destHeight) {
				Y2 -= r.Y2 - destHeight;
				r.Y2 = destHeight;
			}

			if (X1 >= X2 || r.X1 >= r.X2 || Y1 >= Y2 || r.Y1 >= r.Y2)
				*this = r = {};
		}

		void Clear() {
			Empty = true;
			X1 = Y1 = X2 = Y2 = AdvanceX = 0;
		}

		[[nodiscard]] MetricType GetWidth() const { return X2 - X1; }

		[[nodiscard]] MetricType GetHeight() const { return Y2 - Y1; }

		[[nodiscard]] MetricType GetArea() const { return GetWidth() * GetHeight(); }

		[[nodiscard]] bool IsEffectivelyEmpty() const { return Empty || X1 == X2 || Y1 == Y2; }

		operator bool() const {
			return !Empty;
		}

#ifdef _WINDOWS_
		GlyphMetrics& SetFrom(const RECT& r, bool keepAdvanceXIfNotEmpty = true) {
			Empty = !r.left && !r.top && !r.right && !r.bottom;
			if (!Empty) {
				X1 = r.left;
				Y1 = r.top;
				X2 = r.right;
				Y2 = r.bottom;
				if (!keepAdvanceXIfNotEmpty)
					AdvanceX = 0;
			} else {
				X1 = Y2 = X2 = Y2 = AdvanceX = 0;
			}
			return *this;
		}

		operator RECT() const {
			return { static_cast<LONG>(X1), static_cast<LONG>(Y1), static_cast<LONG>(X2), static_cast<LONG>(Y2) };
		}

		struct AsMutableRectPtrType {
			GlyphMetrics& m;
			RECT r;
			const bool keepAdvanceXIfNotEmpty;
			operator RECT* () {
				return &r;
			}

			AsMutableRectPtrType(GlyphMetrics& m, bool keepAdvanceXIfNotEmpty)
				: m(m)
				, r(m)
				, keepAdvanceXIfNotEmpty(keepAdvanceXIfNotEmpty) {

			}
			~AsMutableRectPtrType() { m.SetFrom(r, keepAdvanceXIfNotEmpty); }
		} AsMutableRectPtr(bool keepAdvanceXIfNotEmpty = true) {
			return AsMutableRectPtrType(*this, keepAdvanceXIfNotEmpty);
		}

		[[nodiscard]] struct AsConstRectPtrType {
			RECT r;
			operator RECT* () { return &r; }
			AsConstRectPtrType(const GlyphMetrics& m) : r(m) {}
		} AsConstRectPtr() const {
			return AsConstRectPtrType(*this);
		}
#endif

		template<typename Mul, typename Div>
		GlyphMetrics& Scale(Mul mul, Div div) {
			const auto mMul = static_cast<MetricType>(mul);
			const auto mDiv = static_cast<MetricType>(div);
			X1 = X1 * mMul / mDiv;
			Y1 = Y1 * mMul / mDiv;
			X2 = X2 * mMul / mDiv;
			Y2 = Y2 * mMul / mDiv;
			AdvanceX = AdvanceX * mMul / mDiv;
			return *this;
		}

		GlyphMetrics& Translate(MetricType x, MetricType y) {
			X1 += x;
			X2 += x;
			Y1 += y;
			Y2 += y;
			return *this;
		}

		GlyphMetrics& ExpandToFit(const GlyphMetrics& r) {
			if (r.Empty)
				return *this;
			if (Empty) {
				X1 = r.X1;
				Y1 = r.Y1;
				X2 = r.X2;
				Y2 = r.Y2;
				AdvanceX = r.AdvanceX;
				Empty = false;
			} else {
				const auto prevLeft = X1;
				X1 = (std::min)(X1, r.X1);
				Y1 = (std::min)(Y1, r.Y1);
				X2 = (std::max)(X2, r.X2);
				Y2 = (std::max)(Y2, r.Y2);
				if (prevLeft + AdvanceX > r.X1 + r.AdvanceX)
					AdvanceX = prevLeft + AdvanceX - X1;
				else
					AdvanceX = r.X1 + AdvanceX - X1;
			}
			return *this;
		}
	};
	template<
		typename SrcPixFmt,
		uint32_t ResolverFunction(const SrcPixFmt&),
		typename DestPixFmt,
		typename OpacityType,
		int VerticalDirection = 1
	> class RgbBitmapCopy {
		static_assert(VerticalDirection == 1 || VerticalDirection == -1);

		static constexpr auto Scaler = 0xFFUL;
		static constexpr auto MaxOpacity = (std::numeric_limits<OpacityType>::max)();

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
			constexpr auto DestPixFmtMax = (std::numeric_limits<DestPixFmt>::max)();

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
			constexpr auto DestPixFmtMax = (std::numeric_limits<DestPixFmt>::max)();

			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				*destPtr = static_cast<DestPixFmt>(MaxOpacity * opacityScaled / Scaler);
				++destPtr;
				++srcPtr;
			}
		}

		template<bool ColorIsForeground>
		static inline void DrawLineToL8BinaryOpacity(DestPixFmt* destPtr, const SrcPixFmt* srcPtr, size_t regionWidth, const DestPixFmt& color, double gamma) {
			constexpr auto DestPixFmtMax = (std::numeric_limits<DestPixFmt>::max)();

			while (regionWidth--) {
				const auto opacityScaled = (uint32_t)(std::pow(1.0 * ResolverFunction(*srcPtr) / Scaler, gamma) * Scaler);
				const auto opacityScaled2 = ColorIsForeground ? opacityScaled : Scaler - opacityScaled;
				*destPtr = static_cast<DestPixFmt>((*destPtr * (Scaler - opacityScaled2) + 1 * color * opacityScaled2) / Scaler);
				++destPtr;
				++srcPtr;
			}
		}

	public:
		static inline void CopyTo(const GlyphMetrics& src, const GlyphMetrics& dest, const SrcPixFmt* srcBuf, DestPixFmt* destBuf, SSIZE_T srcWidth, SSIZE_T srcHeight, SSIZE_T destWidth, DestPixFmt fgColor, DestPixFmt bgColor, OpacityType fgOpacity, OpacityType bgOpacity, double gamma) {
			auto destPtrBegin = &destBuf[static_cast<size_t>(1) * dest.Y1 * destWidth + dest.X1];
			auto srcPtrBegin = &srcBuf[static_cast<size_t>(1) * (VerticalDirection == 1 ? src.Y1 : srcHeight - src.Y1 - 1) * srcWidth + src.X1];
			const auto srcPtrDelta = srcWidth * VerticalDirection;
			const auto regionWidth = src.GetWidth();
			const auto regionHeight = src.GetHeight();

			gamma = 1.0 / gamma;

			if constexpr (std::is_integral_v<DestPixFmt>) {
				constexpr auto DestPixFmtMax = (std::numeric_limits<DestPixFmt>::max)();

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
				fgColor.A = fgColor.A * fgOpacity / (std::numeric_limits<OpacityType>::max)();
				bgColor.A = bgColor.A * bgOpacity / (std::numeric_limits<OpacityType>::max)();
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


	class IFontFace {
	public:
		virtual float GetSizePt() const = 0;

		virtual int GetAscent() const = 0;

		virtual int GetLineHeight() const = 0;

		virtual std::vector<char32_t> GetAllCodepoints() const = 0;

		virtual bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const = 0;

		virtual std::vector<std::pair<std::pair<char32_t, char32_t>, int>> GetKerningPairs() const = 0;

		virtual int GetAdjustedAdvanceX(char32_t left, char32_t right) const = 0;

		virtual void Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, uint8_t fgOpacity = 255, uint8_t bgOpacity = 255) const = 0;
	};

	struct TextMeasureResult {
		struct CharacterInfo {
			char32_t Codepoint;
			char32_t Displayed;
			int X;
			int Y;
			GlyphMetrics Metrics;
		};

		GlyphMetrics Occupied;
		std::vector<CharacterInfo> Characters;

		std::shared_ptr<XivRes::MemoryMipmapStream> CreateMipmap(const IFontFace& fontFace, RGBA8888 fgColor, RGBA8888 bgColor, uint8_t fgOpacity = 255, uint8_t bgOpacity = 255) const {
			auto res = std::make_shared<XivRes::MemoryMipmapStream>(Occupied.GetWidth(), Occupied.GetHeight(), 1, XivRes::TextureFormat::A8R8G8B8);
			auto buf = res->View<RGBA8888>();
			bgColor.A = 255 - ((255 - bgColor.A) * (255 - bgOpacity)) / 255;
			std::ranges::fill(buf, bgColor);
			for (const auto& c : Characters) {
				if (c.Metrics.IsEffectivelyEmpty())
					continue;

				fontFace.Draw(c.Displayed, &buf[0], c.X - Occupied.X1, c.Y - Occupied.Y1, res->Width, res->Height, fgColor, {}, fgOpacity, 0);
			}
			return res;
		}
	};

	struct TextMeasurer {
		const IFontFace& FontFace;
		int MaxWidth = (std::numeric_limits<int>::max)();
		int MaxHeight = (std::numeric_limits<int>::max)();
		std::optional<int> LineHeight = std::nullopt;
		const char32_t* FallbackCharacters = U"\u3013-?";
		std::vector<bool> IsCharacterWhiteSpace;
		std::vector<bool> IsCharacterWordBreakPoint;
		std::vector<bool> IsCharacterControlCharacter;

		TextMeasurer(const IFontFace& fontFace)
			: FontFace(fontFace) {
			IsCharacterWhiteSpace.resize(256);
			IsCharacterWhiteSpace[U' '] = true;
			IsCharacterWhiteSpace[U'\r'] = true;
			IsCharacterWhiteSpace[U'\n'] = true;
			IsCharacterWhiteSpace[U'\t'] = true;

			IsCharacterWordBreakPoint.resize(256);
			IsCharacterWordBreakPoint[U' '] = true;
			IsCharacterWordBreakPoint[U'\r'] = true;
			IsCharacterWordBreakPoint[U'\n'] = true;
			IsCharacterWordBreakPoint[U'\t'] = true;

			IsCharacterControlCharacter.resize(0x200d);
			IsCharacterControlCharacter[U'\r'] = true;
			IsCharacterControlCharacter[U'\n'] = true;
			IsCharacterControlCharacter[U'\t'] = true;
			IsCharacterControlCharacter[U'\x200c'] = true;  // Zero-width non joiner
		}

		TextMeasurer& WithMaxWidth(int width = (std::numeric_limits<int>::max)()) {
			MaxWidth = width;
			return *this;
		}

		TextMeasurer& WithMaxHeight(int height = (std::numeric_limits<int>::max)()) {
			MaxHeight = height;
			return *this;
		}

		TextMeasurer& WithLineHeight(std::optional<int> lineHeight = std::nullopt) {
			LineHeight = lineHeight;
			return *this;
		}

		TextMeasurer& WithFallbackCharacters(char32_t* fallbackCharacters) {
			FallbackCharacters = fallbackCharacters;
			return *this;
		}

		TextMeasureResult Measure(const char32_t* pcszString, size_t nLength = (std::numeric_limits<size_t>::max)()) const {
			if (nLength == (std::numeric_limits<size_t>::max)())
				nLength = std::char_traits<char32_t>::length(pcszString);

			TextMeasureResult res{};
			res.Characters.reserve(nLength);
			for (auto pc = pcszString, pc_ = pc + nLength; pc < pc_; pc++) {
				auto c = *pc;
				if (c == '\r') {
					if (pc + 1 < pc_ && *(pc + 1) == '\n')
						continue;
					c = '\n';
				}

				res.Characters.emplace_back(c, c, 0, 0, GlyphMetrics{});
			}

			return Measure(res);
		}

		TextMeasureResult Measure(const char8_t* pcszString, size_t nLength = (std::numeric_limits<size_t>::max)()) const {
			if (nLength == (std::numeric_limits<size_t>::max)())
				nLength = std::char_traits<char8_t>::length(pcszString);

			TextMeasureResult res{};
			res.Characters.reserve(nLength);
			for (auto pc = pcszString, pc_ = pc + nLength; pc < pc_; pc++) {
				char32_t c = *pc;
				if (c == '\r') {
					if (pc + 1 < pc_ && *(pc + 1) == '\n')
						continue;
					c = '\n';
				}

				auto consumed = static_cast<size_t>(pc_ - pc);
				c = Internal::DecodeUtf8(pc, consumed);
				pc += consumed - 1;

				res.Characters.emplace_back(c, c, 0, 0, GlyphMetrics{});
			}

			return Measure(res);
		}

	private:
		TextMeasureResult& Measure(TextMeasureResult& res) const {
			std::vector<size_t> lineBreakIndices;

			if (res.Characters.empty())
				return res;

			for (auto& pair : res.Characters) {
				if (pair.Codepoint < IsCharacterControlCharacter.size() && IsCharacterControlCharacter[pair.Codepoint])
					continue;

				if (!FontFace.GetGlyphMetrics(pair.Displayed, pair.Metrics)) {
					for (auto pfc = FallbackCharacters; (pair.Displayed = *pfc); pfc++) {
						if (FontFace.GetGlyphMetrics(pair.Displayed, pair.Metrics))
							break;
					}
				}
			}

			size_t lastBreakIndex = 0;
			for (size_t i = 1; i < res.Characters.size(); i++) {
				auto& prev = res.Characters[i - 1];
				auto& curr = res.Characters[i];

				if (prev.Codepoint == '\n') {
					lineBreakIndices.push_back(i);
					curr.X = 0;
					curr.Y = prev.Y + LineHeight.value_or(FontFace.GetLineHeight());
				} else {
					curr.X = prev.X + FontFace.GetAdjustedAdvanceX(prev.Displayed, curr.Displayed);
					curr.Y = prev.Y;
				}

				if (prev.Codepoint < IsCharacterWordBreakPoint.size() && IsCharacterWordBreakPoint[prev.Codepoint])
					lastBreakIndex = i;

				if (curr.Codepoint < IsCharacterWhiteSpace.size() && IsCharacterWhiteSpace[curr.Codepoint])
					continue;

				if (curr.X + curr.Metrics.X2 < MaxWidth)
					continue;

				if (!(prev.Codepoint < IsCharacterWhiteSpace.size() && IsCharacterWhiteSpace[prev.Codepoint]) && res.Characters[lastBreakIndex].X > 0)
					i = lastBreakIndex;
				else
					lastBreakIndex = i;
				res.Characters[i].X = 0;
				res.Characters[i].Y = res.Characters[i - 1].Y + LineHeight.value_or(FontFace.GetLineHeight());
				lineBreakIndices.push_back(i);
			}

			for (auto& elem : res.Characters) {
				elem.Metrics.Translate(elem.X, elem.Y);
				res.Occupied.ExpandToFit(elem.Metrics);
			}

			return res;
		}
	};

	class DefaultAbstractFontFace : public IFontFace {

	};

	class FontdataFontFace : public DefaultAbstractFontFace {
		std::shared_ptr<const FontdataStream> m_stream;
		std::vector<std::shared_ptr<const MemoryMipmapStream>> m_mipmapStreams;
		int m_dx = 0;

	public:
		FontdataFontFace(std::shared_ptr<const FontdataStream> stream, std::vector<std::shared_ptr<const MemoryMipmapStream>> mipmapStreams)
			: m_stream(std::move(stream))
			, m_mipmapStreams(std::move(mipmapStreams)) {
			for (const auto& mipmapStream : m_mipmapStreams) {
				if (mipmapStream->Type != TextureFormat::A8R8G8B8)
					throw std::invalid_argument("All mipmap streams must be in A8R8G8B8 format.");
			}

			m_dx = m_stream->TextureWidth();
			for (const auto c : U"HN") {
				const auto pEntry = m_stream->GetFontEntry(c);
				if (!pEntry)
					continue;

				const auto& mipmapStream = *m_mipmapStreams.at(pEntry->TextureFileIndex());
				const auto srcBuf = mipmapStream.View<RGBA8888>();

				for (size_t x = *pEntry->TextureOffsetX, x_ = x + (std::min<int>)(m_dx, *pEntry->BoundingWidth); x < x_; x++) {
					auto pass = true;
					for (size_t y = *pEntry->TextureOffsetY, y_ = *pEntry->TextureOffsetY + *pEntry->BoundingWidth; pass && y < y_; y++) {
						switch (pEntry->TexturePlaneIndex()) {
							case 0:
								pass = GetEffectiveOpacity<0>(srcBuf[y * mipmapStream.Width + x]) == 0;
								break;
							case 1:
								pass = GetEffectiveOpacity<1>(srcBuf[y * mipmapStream.Width + x]) == 0;
								break;
							case 2:
								pass = GetEffectiveOpacity<2>(srcBuf[y * mipmapStream.Width + x]) == 0;
								break;
							case 3:
								pass = GetEffectiveOpacity<3>(srcBuf[y * mipmapStream.Width + x]) == 0;
								break;
							default:
								std::abort();  // Cannot reach
						}
					}
					if (!pass) {
						m_dx = static_cast<int>(x - pEntry->TextureOffsetX) - 1;
						break;
					}
				}
			}
		}

		float GetSizePt() const override {
			return m_stream->Points();
		}

		int GetAscent() const override {
			return m_stream->Ascent();
		}

		int GetLineHeight() const override {
			return m_stream->LineHeight();
		}

		std::vector<char32_t> GetAllCodepoints() const override {
			std::vector<char32_t> res;
			res.reserve(m_stream->GetFontTableEntries().size());
			for (const auto& entry : m_stream->GetFontTableEntries())
				res.emplace_back(entry.Char());
			return res;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			const auto pEntry = m_stream->GetFontEntry(codepoint);
			if (!pEntry) {
				gm.Clear();
				return false;
			}

			gm = GlyphMetricsFromEntry(pEntry);
			return true;
		}

		std::vector<std::pair<std::pair<char32_t, char32_t>, int>> GetKerningPairs() const override {
			std::vector<std::pair<std::pair<char32_t, char32_t>, int>> res;
			res.reserve(m_stream->GetKerningEntries().size());
			for (const auto& entry : m_stream->GetKerningEntries())
				res.emplace_back(std::make_pair(entry.Left(), entry.Right()), entry.RightOffset);
			return res;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			return gm.AdvanceX + m_stream->GetKerningDistance(left, right);
		}

		void Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, uint8_t fgOpacity = 255, uint8_t bgOpacity = 255) const override {
			const auto pEntry = m_stream->GetFontEntry(codepoint);
			if (!pEntry)
				return;

			auto src = GlyphMetrics{ false, *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_mipmapStreams.at(pEntry->TextureFileIndex());
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			const auto srcBuf = mipmapStream.View<RGBA8888>();
			switch (pEntry->TexturePlaneIndex()) {
				case 0:
					return RgbBitmapCopy<RGBA8888, GetEffectiveOpacity<0>, RGBA8888, uint8_t>::CopyTo(src, dest, &srcBuf[0], pBuf, mipmapStream.Width, mipmapStream.Height, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, 1.0);
				case 1:
					return RgbBitmapCopy<RGBA8888, GetEffectiveOpacity<1>, RGBA8888, uint8_t>::CopyTo(src, dest, &srcBuf[0], pBuf, mipmapStream.Width, mipmapStream.Height, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, 1.0);
				case 2:
					return RgbBitmapCopy<RGBA8888, GetEffectiveOpacity<2>, RGBA8888, uint8_t>::CopyTo(src, dest, &srcBuf[0], pBuf, mipmapStream.Width, mipmapStream.Height, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, 1.0);
				case 3:
					return RgbBitmapCopy<RGBA8888, GetEffectiveOpacity<3>, RGBA8888, uint8_t>::CopyTo(src, dest, &srcBuf[0], pBuf, mipmapStream.Width, mipmapStream.Height, destWidth, fgColor, bgColor, fgOpacity, bgOpacity, 1.0);
				default:
					std::abort();  // Cannot reach
			}
		}

	private:
		GlyphMetrics GlyphMetricsFromEntry(const FontdataGlyphEntry* pEntry, int x = 0, int y = 0) const {
			GlyphMetrics src{
				.Empty = false,
				.X1 = x - m_dx,
				.Y1 = y + pEntry->CurrentOffsetY,
				.X2 = src.X1 + pEntry->BoundingWidth,
				.Y2 = src.Y1 + pEntry->BoundingHeight,
				.AdvanceX = pEntry->BoundingWidth + pEntry->NextOffsetX,
			};
			return src;
		}

		template<int ChannelIndex>
		static uint32_t GetEffectiveOpacity(const RGBA8888& src) {
			if constexpr (ChannelIndex == 0)
				return src.R;
			else if constexpr (ChannelIndex == 1)
				return src.G;
			else if constexpr (ChannelIndex == 2)
				return src.B;
			else if constexpr (ChannelIndex == 3)
				return src.A;
			else
				return 0;  // cannot happen
		}
	};
}

void ShowMipmapStream(const XivRes::TextureStream& texStream) {
	static constexpr int Margin = 0;

	struct State {
		const XivRes::TextureStream& texStream;
		std::shared_ptr<XivRes::MipmapStream> stream;

		union {
			struct {
				BITMAPINFOHEADER bmih;
				DWORD bitfields[3];
			};
			BITMAPINFO bmi{};
		};
		std::wstring title = L"Preview";
		int showmode;
		int repeatIndex, mipmapIndex, depthIndex;
		std::vector<uint8_t> buf;
		std::vector<uint8_t> transparent;
		bool closed;

		HWND hwnd;

		POINT renderOffset;

		POINT down;
		POINT downOrig;
		bool dragging;
		bool isLeft;
		bool dragMoved;
		int zoomFactor;

		bool refreshPending = false;

		[[nodiscard]] auto GetZoom() const {
			return std::pow(2, 1. * zoomFactor / WHEEL_DELTA / 8);
		}

		void LoadMipmap(int repeatIndex, int mipmapIndex, int depthIndex) {
			this->repeatIndex = repeatIndex = (std::min)(texStream.GetRepeatCount() - 1, (std::max)(0, repeatIndex));
			this->mipmapIndex = mipmapIndex = (std::min)(texStream.GetMipmapCount() - 1, (std::max)(0, mipmapIndex));
			this->depthIndex = depthIndex = (std::min)(DepthCount() - 1, (std::max)(0, depthIndex));

			stream = XivRes::MemoryMipmapStream::AsARGB8888(*texStream.GetMipmap(repeatIndex, mipmapIndex));
			const auto planeSize = XivRes::TextureRawDataLength(stream->Type, stream->Width, stream->Height, 1);
			buf = ReadStreamIntoVector<uint8_t>(*stream, depthIndex * planeSize, planeSize);
			{
				transparent = buf;
				const auto w = static_cast<size_t>(stream->Width);
				const auto h = static_cast<size_t>(stream->Height);
				const auto view = std::span(reinterpret_cast<XivRes::RGBA8888*>(&transparent[0]), w * h);
				for (size_t i = 0; i < h; ++i) {
					for (size_t j = 0; j < w; ++j) {
						auto& v = view[i * w + j];
						auto bg = (i / 8 + j / 8) % 2 ? XivRes::RGBA8888(255, 255, 255, 255) : XivRes::RGBA8888(150, 150, 150, 255);
						v.R = (v.R * v.A + bg.R * (255U - v.A)) / 255U;
						v.G = (v.G * v.A + bg.G * (255U - v.A)) / 255U;
						v.B = (v.B * v.A + bg.B * (255U - v.A)) / 255U;
					}
				}
			}

			bmih.biSize = sizeof bmih;
			bmih.biWidth = stream->Width;
			bmih.biHeight = -stream->Height;
			bmih.biPlanes = 1;
			bmih.biBitCount = 32;
			bmih.biCompression = BI_BITFIELDS;

			ClipPan();
			UpdateTitle();
		}

		void Draw(HDC hdc, const RECT& clip) {
			RECT wrt;
			GetClientRect(hwnd, &wrt);
			const auto newdc = !hdc;
			if (newdc)
				hdc = GetDC(hwnd);
			const auto zoom = GetZoom();
			const auto dw = static_cast<int>(stream->Width * zoom);
			const auto dh = static_cast<int>(stream->Height * zoom);
			IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);
			if (showmode == 0)
				SetStretchBltMode(hdc, zoomFactor < 0 ? HALFTONE : COLORONCOLOR);
			SetBrushOrgEx(hdc, renderOffset.x, renderOffset.y, nullptr);
			switch (showmode) {
				case 0:
				case 1:
					reinterpret_cast<XivRes::RGBA8888*>(&bitfields[0])->SetFrom(0xff, 0, 0, 0);
					reinterpret_cast<XivRes::RGBA8888*>(&bitfields[1])->SetFrom(0, 0xff, 0, 0);
					reinterpret_cast<XivRes::RGBA8888*>(&bitfields[2])->SetFrom(0, 0, 0xff, 0);
					break;
				case 2:
					for (auto& bitfield : bitfields)
						reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0xff, 0, 0, 0);
					break;
				case 3:
					for (auto& bitfield : bitfields)
						reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0, 0xff, 0, 0);
					break;
				case 4:
					for (auto& bitfield : bitfields)
						reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0, 0, 0xff, 0);
					break;
				case 5:
					for (auto& bitfield : bitfields)
						reinterpret_cast<XivRes::RGBA8888*>(&bitfield)->SetFrom(0, 0, 0, 0xff);
					break;
			}
			StretchDIBits(hdc, renderOffset.x, renderOffset.y, dw, dh, 0, 0, stream->Width, stream->Height, showmode == 0 ? &transparent[0] : &buf[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
			if (renderOffset.x > 0) {
				const auto rt = RECT{ 0, clip.top, renderOffset.x, clip.bottom };
				FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
			}
			if (renderOffset.x + dw < wrt.right - wrt.left) {
				const auto rt = RECT{ renderOffset.x + dw, clip.top, wrt.right - wrt.left, clip.bottom };
				FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
			}
			if (renderOffset.y > 0) {
				const auto rt = RECT{ clip.left, 0, clip.right, renderOffset.y };
				FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
			}
			if (renderOffset.y + dh < wrt.bottom - wrt.top) {
				const auto rt = RECT{ clip.left, renderOffset.y + dh, clip.right, wrt.bottom - wrt.top };
				FillRect(hdc, &rt, GetStockBrush(WHITE_BRUSH));
			}
			if (newdc)
				ReleaseDC(hwnd, hdc);
		}

		void ChangeZoom(int newZoomFactor, int nmx, int nmy) {
			POINT nm = { nmx, nmy };
			ScreenToClient(hwnd, &nm);
			const double mx = nm.x, my = nm.y;
			const auto ox = (mx - renderOffset.x) / GetZoom();
			const auto oy = (my - renderOffset.y) / GetZoom();

			zoomFactor = newZoomFactor;

			renderOffset.x = static_cast<int>(mx - ox * GetZoom());
			renderOffset.y = static_cast<int>(my - oy * GetZoom());

			ClipPan();
			UpdateTitle();
		}

		void ClipPan() {
			RECT rt;
			GetClientRect(hwnd, &rt);
			const auto zwidth = static_cast<int>(stream->Width * GetZoom());
			const auto zheight = static_cast<int>(stream->Height * GetZoom());
			if (renderOffset.x < rt.right - rt.left - Margin - zwidth)
				renderOffset.x = rt.right - rt.left - Margin - zwidth;
			if (renderOffset.x > Margin)
				renderOffset.x = Margin;
			if (renderOffset.y < rt.bottom - rt.top - Margin - zheight)
				renderOffset.y = rt.bottom - rt.top - Margin - zheight;
			if (renderOffset.y > Margin)
				renderOffset.y = Margin;
			InvalidateRect(hwnd, nullptr, FALSE);
			refreshPending = true;
		}

		void UpdateTitle() {
			auto w = std::format(L"{} (r{}/{} m{}/{} d{}/{}): {:.2f}%", title, 1 + repeatIndex, texStream.GetRepeatCount(), 1 + mipmapIndex, texStream.GetMipmapCount(), 1 + depthIndex, DepthCount(), 100. * GetZoom());
			switch (showmode) {
				case 0:
					w += L" (All channels)";
					break;
				case 1:
					w += L" (No alpha)";
					break;
				case 2:
					w += L" (Red)";
					break;
				case 3:
					w += L" (Green)";
					break;
				case 4:
					w += L" (Blue)";
					break;
				case 5:
					w += L" (Alpha)";
					break;
			}
			SetWindowTextW(hwnd, w.c_str());
		}

		int DepthCount() const {
			return (std::max)(1, texStream.GetDepth() / (1 << mipmapIndex));
		}
	} state{ .texStream = texStream };

	state.LoadMipmap(0, 0, 0);

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hInstance = GetModuleHandleW(nullptr);
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
	wcex.lpszClassName = L"Sqex::Texture::MipmapStream::Show";

	wcex.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		if (const auto pState = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
			auto& state = *pState;
			switch (msg) {
				case WM_CREATE:
				{
					state.hwnd = hwnd;
					state.UpdateTitle();
					return 0;
				}

				case WM_MOUSEWHEEL:
				{
					state.ChangeZoom(state.zoomFactor + GET_WHEEL_DELTA_WPARAM(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
					return 0;
				}

				case WM_PAINT:
				{
					PAINTSTRUCT ps;
					const auto hdc = BeginPaint(hwnd, &ps);
					state.Draw(hdc, ps.rcPaint);
					EndPaint(hwnd, &ps);
					state.refreshPending = false;
					return 0;
				}
				case WM_KEYDOWN:
				{
					switch (wParam) {
						case VK_ESCAPE:
							DestroyWindow(hwnd);
							return 0;
						case VK_LEFT:
							state.renderOffset.x += 8;
							state.ClipPan();
							return 0;
						case VK_RIGHT:
							state.renderOffset.x -= 8;
							state.ClipPan();
							return 0;
						case VK_UP:
							state.renderOffset.y += 8;
							state.ClipPan();
							return 0;
						case VK_DOWN:
							state.renderOffset.y -= 8;
							state.ClipPan();
							return 0;
						case 'Q':
							if (!state.refreshPending)
								state.LoadMipmap(state.repeatIndex - 1, state.mipmapIndex, state.depthIndex);
							return 0;
						case 'W':
							if (!state.refreshPending)
								state.LoadMipmap(state.repeatIndex + 1, state.mipmapIndex, state.depthIndex);
							return 0;
						case 'A':
							if (!state.refreshPending)
								state.LoadMipmap(state.repeatIndex, state.mipmapIndex - 1, state.depthIndex);
							return 0;
						case 'S':
							if (!state.refreshPending)
								state.LoadMipmap(state.repeatIndex, state.mipmapIndex + 1, state.depthIndex);
							return 0;
						case 'Z':
							if (!state.refreshPending)
								state.LoadMipmap(state.repeatIndex, state.mipmapIndex, (state.depthIndex + state.DepthCount() - 1) % state.DepthCount());
							return 0;
						case 'X':
							if (!state.refreshPending)
								state.LoadMipmap(state.repeatIndex, state.mipmapIndex, (state.depthIndex + 1) % state.DepthCount());
							return 0;
					}
					break;
				}

				case WM_LBUTTONDOWN:
				case WM_RBUTTONDOWN:
				{
					if (!state.dragging) {
						state.dragging = true;
						state.dragMoved = false;
						state.isLeft = msg == WM_LBUTTONDOWN;
						state.downOrig = { GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam) };
						ClientToScreen(hwnd, &state.downOrig);
						if (state.isLeft) {
							SetCursorPos(state.down.x = GetSystemMetrics(SM_CXSCREEN) / 2,
								state.down.y = GetSystemMetrics(SM_CYSCREEN) / 2);
						} else {
							state.down = state.downOrig;
						}
						ShowCursor(FALSE);
						SetCapture(hwnd);
					}
					return 0;
				}

				case WM_MOUSEMOVE:
				{
					if (state.dragging) {
						RECT rt;
						GetClientRect(hwnd, &rt);

						POINT screenCursorPos = { GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam) };
						ClientToScreen(hwnd, &screenCursorPos);

						auto displaceX = screenCursorPos.x - state.down.x;
						auto displaceY = screenCursorPos.y - state.down.y;
						const auto speed = (state.isLeft ? 1 : 4);
						if (!state.dragMoved) {
							if (!displaceX && !displaceY)
								return 0;
							state.dragMoved = true;
						}
						const auto zwidth = static_cast<int>(state.stream->Width * state.GetZoom());
						const auto zheight = static_cast<int>(state.stream->Height * state.GetZoom());
						if (state.renderOffset.x + displaceX * speed < rt.right - rt.left - Margin - zwidth)
							displaceX = (rt.right - rt.left - Margin - zwidth - state.renderOffset.x) / speed;
						if (state.renderOffset.x + displaceX * speed > Margin)
							displaceX = (Margin - state.renderOffset.x) / speed;
						if (state.renderOffset.y + displaceY * speed < rt.bottom - rt.top - Margin - zheight)
							displaceY = (rt.bottom - rt.top - Margin - zheight - state.renderOffset.y) / speed;
						if (state.renderOffset.y + displaceY * speed > Margin)
							displaceY = (Margin - state.renderOffset.y) / speed;

						if (state.isLeft)
							SetCursorPos(state.down.x, state.down.y);
						else {
							state.down.x += displaceX;
							state.down.y += displaceY;
						}
						state.renderOffset.x += displaceX * speed;
						state.renderOffset.y += displaceY * speed;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}

				case WM_LBUTTONUP:
				case WM_RBUTTONUP:
				{
					if (!state.dragging || state.isLeft != (msg == WM_LBUTTONUP))
						return false;
					ReleaseCapture();
					SetCursorPos(state.downOrig.x, state.downOrig.y);
					ShowCursor(TRUE);

					state.dragging = false;
					if (!state.dragMoved) {
						if (state.isLeft) {
							state.showmode = (state.showmode + 1) % 6;
							state.UpdateTitle();
							InvalidateRect(hwnd, nullptr, FALSE);
						} else {
							state.renderOffset = {};
							state.ChangeZoom(0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
						}
					}
					return 0;
				}

				case WM_SIZE:
				{
					state.ClipPan();
					return 0;
				}

				case WM_NCDESTROY:
				{
					state.closed = true;
				}
			}
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	};
	RegisterClassExW(&wcex);

	const auto unreg = XivRes::Internal::CallOnDestruction([&]() {
		UnregisterClassW(wcex.lpszClassName, wcex.hInstance);
	});

	RECT rc{ 0, 0, texStream.GetWidth(), texStream.GetHeight() };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	state.hwnd = CreateWindowExW(0, wcex.lpszClassName, state.title.c_str(), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		(std::max)(640L, (std::min)(1920L, rc.right - rc.left)),
		(std::max)(480L, (std::min)(1080L, rc.bottom - rc.top)),
		nullptr, nullptr, nullptr, nullptr);
	if (!state.hwnd)
		throw std::system_error(GetLastError(), std::system_category());
	SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));

	state.UpdateTitle();
	ShowWindow(state.hwnd, SW_SHOW);

	MSG msg{};
	while (!state.closed && GetMessageW(&msg, nullptr, 0, 0)) {
		if (msg.hwnd != state.hwnd && IsDialogMessageW(msg.hwnd, &msg))
			continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (msg.message == WM_QUIT)
		PostQuitMessage(0);
}

static const auto* const pszTestString = (
	u8"Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
	u8"Lowercase: abcdefghijklmnopqrstuvwxyz\n"
	u8"Numbers: 0123456789 ０１２３４５６７８９\n"
	u8"SymbolsH: `~!@#$%^&*()_+-=[]{}\\|;':\",./<>?\n"
	u8"SymbolsF: ｀～！＠＃＄％＾＆＊（）＿＋－＝［］｛｝￦｜；＇：＂，．／＜＞？\n"
	u8"Hiragana: あかさたなはまやらわ\n"
	u8"KatakanaH: ｱｶｻﾀﾅﾊﾏﾔﾗﾜ\n"
	u8"KatakanaF: アカサタナハマヤラワ\n"
	u8"Hangul: 가나다라마바사아자차카타파하\n"
	u8"Hangul: ㄱㄴㄷㄹㅁㅂㅅㅇㅈㅊㅋㅌㅍㅎ\n"
	u8"Chinese: 天地玄黄，宇宙洪荒。\n"
	u8"\n"
	u8"<<SupportedUnicode>>\n"
	u8"π™′＾¿¿‰øØ×∞∩£¥¢Ð€ªº†‡¤ ŒœŠšŸÅωψ↑↓→←⇔⇒♂♀♪¶§±＜＞≥≤≡÷½¼¾©®ª¹²³\n"
	u8"※⇔｢｣«»≪≫《》【】℉℃‡。·••‥…¨°º‰╲╳╱☁☀☃♭♯✓〃¹²³\n"
	u8"●◎○■□▲△▼▽∇♥♡★☆◆◇♦♦♣♠♤♧¶αß∇ΘΦΩδ∂∃∀∈∋∑√∝∞∠∟∥∪∩∨∧∫∮∬\n"
	u8"∴∵∽≒≠≦≤≥≧⊂⊃⊆⊇⊥⊿⌒─━│┃│¦┗┓└┏┐┌┘┛├┝┠┣┤┥┫┬┯┰┳┴┷┸┻╋┿╂┼￢￣，－．／：；＜＝＞［＼］＿｀｛｜｝～＠\n"
	u8"⑴⑵⑶⑷⑸⑹⑺⑻⑼⑽⑾⑿⒀⒁⒂⒃⒄⒅⒆⒇⓪①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳\n"
	u8"₀₁₂₃₄₅₆₇₈₉№ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅰⅱⅲⅳⅴⅵⅶⅷⅸⅹ０１２３４５６７８９！？＂＃＄％＆＇（）＊＋￠￤￥\n"
	u8"ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ\n"
	u8"\n"
	u8"<<GameSpecific>>\n"
	u8" \n"
	u8"\n"
	u8"\n"
	u8"\n"
	u8"\n"
	u8"<<Kerning>>\n"
	u8"AC AG AT AV AW AY LT LV LW LY TA Ta Tc Td Te Tg To VA Va Vc Vd Ve Vg Vm Vo Vp Vq Vu\n"
	u8"A\u200cC A\u200cG A\u200cT A\u200cV A\u200cW A\u200cY L\u200cT L\u200cV L\u200cW L\u200cY T\u200cA T\u200ca T\u200cc T\u200cd T\u200ce T\u200cg T\u200co V\u200cA V\u200ca V\u200cc V\u200cd V\u200ce V\u200cg V\u200cm V\u200co V\u200cp V\u200cq V\u200cu\n"
	u8"WA We Wq YA Ya Yc Yd Ye Yg Ym Yn Yo Yp Yq Yr Yu eT oT\n"
	u8"W\u200cA W\u200ce W\u200cq Y\u200cA Y\u200ca Y\u200cc Y\u200cd Y\u200ce Y\u200cg Y\u200cm Y\u200cn Y\u200co Y\u200cp Y\u200cq Y\u200cr Y\u200cu e\u200cT o\u200cT\n"
	u8"Az Fv Fw Fy TV TW TY Tv Tw Ty VT WT YT tv tw ty vt wt yt\n"
	u8"A\u200cz F\u200cv F\u200cw F\u200cy T\u200cV T\u200cW T\u200cY T\u200cv T\u200cw T\u200cy V\u200cT W\u200cT Y\u200cT t\u200cv t\u200cw t\u200cy v\u200ct w\u200ct y\u200ct\n"
);

int main() {
	std::vector<char> tmp;
	system("chcp 65001");

	XivRes::GameReader gameReader(R"(C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game)");
	XivRes::GameReader gameReaderCn(R"(C:\Program Files (x86)\SNDA\FFXIV\game)");
	XivRes::GameReader gameReaderKr(R"(C:\Program Files (x86)\FINAL FANTASY XIV - KOREA\game)");

	auto fdts = std::make_shared<XivRes::FontdataStream>(*gameReader.GetFileStream("common/font/AXIS_36.fdt"));
	std::vector<std::shared_ptr<const XivRes::MemoryMipmapStream>> streams;
	for (int i = 1; i <= 7; i++)
		streams.emplace_back(XivRes::MemoryMipmapStream::AsARGB8888(*XivRes::TextureStream(gameReader.GetFileStream(std::format("common/font/font{}.tex", i))).GetMipmap(0, 0)));

	auto face = std::make_shared<XivRes::FontGenerator::FontdataFontFace>(fdts, streams);
	auto tex = XivRes::FontGenerator::TextMeasurer(*face)
		// .WithMaxWidth(320)
		.Measure(pszTestString)
		.CreateMipmap(*face, XivRes::RGBA8888(255, 255, 255, 255), XivRes::RGBA8888(0, 0, 0, 200), 255, 150)
		->ToSingleTextureStream();
	ShowMipmapStream(*tex);
	return 0;
}
