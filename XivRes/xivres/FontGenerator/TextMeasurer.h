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

		std::shared_ptr<XivRes::MemoryMipmapStream> CreateMipmap(const IFixedSizeFont& fontFace, RGBA8888 fgColor, RGBA8888 bgColor, int pad = 0) const {
			auto res = std::make_shared<XivRes::MemoryMipmapStream>(pad * 2 + Occupied.GetWidth(), pad * 2 + Occupied.GetHeight(), 1, XivRes::TextureFormat::A8R8G8B8);
			auto buf = res->View<RGBA8888>();
			std::ranges::fill(buf, bgColor);
			for (const auto& c : Characters) {
				if (c.Metrics.IsEffectivelyEmpty())
					continue;

				fontFace.Draw(c.Displayed, &buf[0], pad + c.X - Occupied.X1, pad + c.Y - Occupied.Y1, res->Width, res->Height, fgColor, {});
			}
			return res;
		}
	};

	struct TextMeasurer {
		const IFixedSizeFont& FontFace;
		int MaxWidth = (std::numeric_limits<int>::max)();
		int MaxHeight = (std::numeric_limits<int>::max)();
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
}

#endif
