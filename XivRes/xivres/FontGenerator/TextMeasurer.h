#ifndef _XIVRES_FONTGENERATOR_TEXTMEASURER_H_
#define _XIVRES_FONTGENERATOR_TEXTMEASURER_H_

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {
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

		void DrawTo(XivRes::MemoryMipmapStream& mipmapStream, const IFixedSizeFont& fontFace, int x, int y, RGBA8888 fgColor, RGBA8888 bgColor) const {
			const auto buf = mipmapStream.View<RGBA8888>();
			for (const auto& c : Characters) {
				if (c.Metrics.IsEffectivelyEmpty())
					continue;

				fontFace.Draw(
					c.Displayed,
					&buf[0],
					x + c.X, y + c.Y,
					mipmapStream.Width, mipmapStream.Height,
					fgColor,
					{});
			}
		}

		std::shared_ptr<XivRes::MemoryMipmapStream> CreateMipmap(const IFixedSizeFont& fontFace, RGBA8888 fgColor, RGBA8888 bgColor, int pad = 0) const {
			auto res = std::make_shared<XivRes::MemoryMipmapStream>(
				pad * 2 + Occupied.X2 - (std::min)(0, Occupied.X1),
				pad * 2 + Occupied.Y2 - (std::min)(0, Occupied.Y1),
				1,
				XivRes::TextureFormat::A8R8G8B8);
			std::ranges::fill(res->View<RGBA8888>(), bgColor);
			DrawTo(
				*res,
				fontFace,
				pad - (std::min)(0, Occupied.X1),
				pad - (std::min)(0, Occupied.Y1),
				fgColor,
				bgColor);
			return res;
		}
	};

	struct TextMeasurer {
		const IFixedSizeFont& FontFace;
		int MaxWidth = (std::numeric_limits<int>::max)();
		int MaxHeight = (std::numeric_limits<int>::max)();
		bool UseKerning = true;
		std::optional<int> LineHeight = std::nullopt;
		const char32_t* FallbackCharacters = U"\u3013-?";
		std::vector<bool> IsCharacterWhiteSpace;
		std::vector<bool> IsCharacterWordBreakPoint;
		std::vector<bool> IsCharacterControlCharacter;

		TextMeasurer(const IFixedSizeFont& fontFace)
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

		TextMeasurer& WithUseKerning(bool use) {
			UseKerning = use;
			return *this;
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

		template <class TStringElem, class TStringTraits = std::char_traits<TStringElem>, class TStringAlloc = std::allocator<TStringElem>>
		TextMeasureResult Measure(const std::basic_string<TStringElem, TStringTraits, TStringAlloc>& pcszString) const {
			return Measure(&pcszString[0], pcszString.size());
		}

		template <class TStringElem, class TStringTraits = std::char_traits<TStringElem>>
		TextMeasureResult Measure(const std::basic_string_view<TStringElem, TStringTraits>& pcszString) const {
			return Measure(&pcszString[0], pcszString.size());
		}

		template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
		TextMeasureResult Measure(const T* pcszString, size_t nLength = (std::numeric_limits<size_t>::max)()) const {
			if (nLength == (std::numeric_limits<size_t>::max)())
				nLength = std::char_traits<T>::length(pcszString);

			TextMeasureResult res{};
			res.Characters.reserve(nLength);
			for (auto pc = pcszString, pc_ = pc + nLength; pc < pc_; pc++) {
				char32_t c = *pc;
				if (c == '\r') {
					if (pc + 1 < pc_ && *(pc + 1) == '\n')
						continue;
					c = '\n';
				}

				pc += Unicode::Decode(c, pc, pc_ - pc) - 1;
				res.Characters.emplace_back(c, c, 0, 0, GlyphMetrics{});
			}

			return Measure(res);
		}

	private:
		TextMeasureResult& Measure(TextMeasureResult& res) const {
			std::vector<size_t> lineBreakIndices;

			if (res.Characters.empty())
				return res;

			for (auto& curr : res.Characters) {
				if (curr.Codepoint < IsCharacterControlCharacter.size() && IsCharacterControlCharacter[curr.Codepoint])
					continue;

				if (!FontFace.GetGlyphMetrics(curr.Displayed, curr.Metrics)) {
					for (auto pfc = FallbackCharacters; (curr.Displayed = *pfc); pfc++) {
						if (FontFace.GetGlyphMetrics(curr.Displayed, curr.Metrics))
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
					curr.X = prev.X + (UseKerning ? FontFace.GetAdjustedAdvanceX(prev.Displayed, curr.Displayed) : prev.Metrics.AdvanceX);
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
}

#endif
