#ifndef _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {
	struct WrapModifiers {
		std::vector<std::pair<char32_t, char32_t>> Codepoints;
		int LetterSpacing = 0;
		int HorizontalOffset = 0;
		int BaselineShift = 0;
		float Gamma = 1.f;
	};

	class WrappingFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::set<char32_t> Codepoints;
			std::map<char32_t, int> NetHorizontalOffsets;
			std::map<char32_t, GlyphMetrics> AllGlyphMetrics;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			WrapModifiers Modifiers;
		};

		std::shared_ptr<const IFixedSizeFont> m_font;
		std::shared_ptr<const InfoStruct> m_info;

	public:
		WrappingFixedSizeFont(std::shared_ptr<const IFixedSizeFont> font, const WrapModifiers& wrapModifiers)
			: m_font(std::move(font)) {

			auto info = std::make_shared<InfoStruct>();
			info->Modifiers = wrapModifiers;
			if (!wrapModifiers.Codepoints.empty()) {
				std::set<char32_t> codepoints;
				for (const auto& [c1, c2] : info->Modifiers.Codepoints) {
					for (auto c = c1; c <= c2; c++)
						codepoints.insert(c);
				}
				std::ranges::set_intersection(codepoints, m_font->GetAllCodepoints(), std::inserter(info->Codepoints, info->Codepoints.begin()));

				info->KerningPairs = m_font->GetAllKerningPairs();
				for (auto it = info->KerningPairs.begin(); it != info->KerningPairs.end(); ) {
					if (info->Codepoints.contains(it->first.first) && info->Codepoints.contains(it->first.second))
						++it;
					else
						it = info->KerningPairs.erase(it);
				}
			} else {
				info->Codepoints = m_font->GetAllCodepoints();
				info->KerningPairs = m_font->GetAllKerningPairs();
			}

			std::map<Unicode::UnicodeBlocks::NegativeLsbGroup, std::map<char32_t, int>> negativeLsbChars;
			GlyphMetrics gmSrc;
			for (const auto& [codepoint, gmSrc] : m_font->GetAllGlyphMetrics()) {
				if (!info->Codepoints.contains(codepoint))
					continue;

				auto& gmDst = info->AllGlyphMetrics[codepoint];
				gmDst = gmSrc;

				const auto remainingOffset = gmSrc.X1 + info->Modifiers.HorizontalOffset;
				if (remainingOffset >= 0) {
					info->NetHorizontalOffsets[codepoint] = info->Modifiers.HorizontalOffset;
					gmDst.AdvanceX = gmSrc.AdvanceX + info->Modifiers.LetterSpacing;

				} else {
					info->NetHorizontalOffsets[codepoint] = -gmSrc.X1;
					gmDst.AdvanceX = gmSrc.AdvanceX + info->Modifiers.LetterSpacing - remainingOffset;

					do {
						const auto& block = Unicode::UnicodeBlocks::GetCorrespondingBlock(codepoint);
						if (block.NegativeLsbGroup == Unicode::UnicodeBlocks::None)
							break;

						if (block.Flags & Unicode::UnicodeBlocks::UsedWithCombining)
							break;

						negativeLsbChars[block.NegativeLsbGroup][codepoint] = remainingOffset;
					} while (false);
				}

				gmDst.Translate(info->NetHorizontalOffsets.at(codepoint), info->Modifiers.BaselineShift);
			}

#pragma warning(push)
#pragma warning(disable: 26812)

			for (const auto& [group, chars] : negativeLsbChars) {
				for (const auto& [rightc, offset] : chars) {
					for (const auto& block : Unicode::UnicodeBlocks::Blocks) {
						if (block.NegativeLsbGroup != group && (group != Unicode::UnicodeBlocks::Combining || !(block.Flags & Unicode::UnicodeBlocks::UsedWithCombining)))
							continue;
						for (auto leftc = block.First; leftc <= block.Last; leftc++) {
							if (!info->Codepoints.contains(leftc))
								continue;
							info->KerningPairs[std::make_pair(leftc, rightc)] += offset;
						}
					}
				}
			}

#pragma warning(pop)

			m_info = std::move(info);
		}

		WrappingFixedSizeFont() = default;
		WrappingFixedSizeFont(const WrappingFixedSizeFont& r) = default;
		WrappingFixedSizeFont(WrappingFixedSizeFont&& r) = default;
		WrappingFixedSizeFont& operator=(const WrappingFixedSizeFont& r) = default;
		WrappingFixedSizeFont& operator=(WrappingFixedSizeFont&& r) = default;

		std::string GetFamilyName() const override {
			return m_font->GetFamilyName();
		}

		std::string GetSubfamilyName() const override {
			return m_font->GetSubfamilyName();
		}

		int GetRecommendedHorizontalOffset() const override {
			return 0;
		}

		int GetMaximumRequiredHorizontalOffset() const override {
			return 0;
		}

		float GetSize() const override {
			return m_font->GetSize();
		}

		int GetAscent() const override {
			return m_font->GetAscent();
		}

		int GetLineHeight() const override {
			return m_font->GetLineHeight();
		}

		const std::set<char32_t>& GetAllCodepoints() const override {
			return m_info->Codepoints;
		}

		const std::map<char32_t, GlyphMetrics>& GetAllGlyphMetrics() const override {
			return m_info->AllGlyphMetrics;
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			if (!m_info->Codepoints.contains(c))
				return nullptr;

			return m_font->GetGlyphUniqid(c);
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			return m_info->KerningPairs;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, float gamma) const override {
			GlyphMetrics gm;
			if (!m_info->Codepoints.contains(codepoint))
				return false;

			return m_font->Draw(codepoint, pBuf, drawX + m_info->NetHorizontalOffsets.at(codepoint), drawY + m_info->Modifiers.BaselineShift, destWidth, destHeight, fgColor, bgColor, gamma * m_info->Modifiers.Gamma);
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity, float gamma) const override {
			GlyphMetrics gm;
			if (!m_info->Codepoints.contains(codepoint))
				return false;

			return m_font->Draw(codepoint, pBuf, stride, drawX + m_info->NetHorizontalOffsets.at(codepoint), drawY + m_info->Modifiers.BaselineShift, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity, gamma * m_info->Modifiers.Gamma);
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			auto res = std::make_shared<WrappingFixedSizeFont>(*this);
			res->m_font = m_font->GetThreadSafeView();
			return res;
		}
	};
}

#endif
