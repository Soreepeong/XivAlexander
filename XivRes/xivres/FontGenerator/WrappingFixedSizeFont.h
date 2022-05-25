#ifndef _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {
	struct WrapModifiers {
		std::vector<std::pair<char32_t, char32_t>> Codepoints;
		int LetterSpacing = 0;
		int HorizontalOffset = 0;
		int BaselineShift = 0;
	};

	class WrappingFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::set<char32_t> Codepoints;
			int LetterSpacing = 0;
			int HorizontalOffset = 0;
			int BaselineShift = 0;
		};

		std::shared_ptr<const IFixedSizeFont> m_font;
		std::shared_ptr<const InfoStruct> m_info;

		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		WrappingFixedSizeFont(std::shared_ptr<const IFixedSizeFont> font, const WrapModifiers& wrapModifiers)
			: m_font(std::move(font)) {

			auto info = std::make_shared<InfoStruct>();
			info->LetterSpacing = wrapModifiers.LetterSpacing;
			info->HorizontalOffset = wrapModifiers.HorizontalOffset;
			info->BaselineShift = wrapModifiers.BaselineShift;

			std::set<char32_t> codepoints;
			for (const auto& [c1, c2] : wrapModifiers.Codepoints) {
				for (auto c = c1; c <= c2; c++)
					codepoints.insert(c);
			}
			std::ranges::set_intersection(codepoints, m_font->GetAllCodepoints(), std::inserter(info->Codepoints, info->Codepoints.begin()));

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

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			if (!m_font->GetGlyphMetrics(codepoint, gm))
				return false;

			const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
			if (remainingOffset >= 0) {
				gm.Translate(m_info->HorizontalOffset, m_info->BaselineShift);
				gm.AdvanceX += m_info->LetterSpacing;
			} else {
				gm.Translate(-gm.X1, m_info->BaselineShift);
				gm.AdvanceX += m_info->LetterSpacing - gm.X1;
			}

			return true;
		}

		const void* GetBaseFontGlyphUniqid(char32_t c) const override {
			if (!m_info->Codepoints.contains(c))
				return nullptr;

			return m_font->GetBaseFontGlyphUniqid(c);
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			if (m_kerningPairs)
				return *m_kerningPairs;

			std::map<char32_t, int> NetHorizontalOffsets;
			std::map<char32_t, GlyphMetrics> AllGlyphMetrics;
			std::map<Unicode::UnicodeBlocks::NegativeLsbGroup, std::map<char32_t, int>> negativeLsbChars;

			for (const auto codepoint : m_info->Codepoints) {
				GlyphMetrics gm;
				if (!m_font->GetGlyphMetrics(codepoint, gm))
					continue;

				const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
				if (remainingOffset >= 0) {
					gm.AdvanceX = gm.AdvanceX + m_info->LetterSpacing;
					gm.Translate(m_info->HorizontalOffset, m_info->BaselineShift);

				} else {
					gm.AdvanceX = gm.AdvanceX + m_info->LetterSpacing - gm.X1;
					gm.Translate(-gm.X1, m_info->BaselineShift);

					do {
						const auto& block = Unicode::UnicodeBlocks::GetCorrespondingBlock(codepoint);
						if (block.NegativeLsbGroup == Unicode::UnicodeBlocks::None)
							break;

						if (block.Flags & Unicode::UnicodeBlocks::UsedWithCombining)
							break;

						negativeLsbChars[block.NegativeLsbGroup][codepoint] = remainingOffset;
					} while (false);
				}
			}

			m_kerningPairs.emplace(m_font->GetAllKerningPairs());
#pragma warning(push)
#pragma warning(disable: 26812)
			for (const auto& [group, chars] : negativeLsbChars) {
				for (const auto& [rightc, offset] : chars) {
					for (const auto& block : Unicode::UnicodeBlocks::Blocks) {
						if (block.NegativeLsbGroup != group && (group != Unicode::UnicodeBlocks::Combining || !(block.Flags & Unicode::UnicodeBlocks::UsedWithCombining)))
							continue;
#pragma warning(pop)
						for (auto leftc = block.First; leftc <= block.Last; leftc++) {
							if (!m_info->Codepoints.contains(leftc))
								continue;
							(*m_kerningPairs)[std::make_pair(leftc, rightc)] += offset;
						}
					}
				}
			}

			for (auto it = m_kerningPairs->begin(); it != m_kerningPairs->end(); ) {
				if (m_info->Codepoints.contains(it->first.first) && m_info->Codepoints.contains(it->first.second) && it->second != 0)
					++it;
				else
					it = m_kerningPairs->erase(it);
			}

			return *m_kerningPairs;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			GlyphMetrics gm;
			if (!m_font->GetGlyphMetrics(codepoint, gm))
				return false;

			const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
			if (remainingOffset >= 0)
				drawX += m_info->HorizontalOffset;
			else
				drawX -= gm.X1;
			drawY += m_info->BaselineShift;

			return m_font->Draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor);
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			GlyphMetrics gm;
			if (!m_font->GetGlyphMetrics(codepoint, gm))
				return false;

			const auto remainingOffset = gm.X1 + m_info->HorizontalOffset;
			if (remainingOffset >= 0)
				drawX += m_info->HorizontalOffset;
			else
				drawX -= gm.X1;
			drawY += m_info->BaselineShift;

			return m_font->Draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			auto res = std::make_shared<WrappingFixedSizeFont>(*this);
			res->m_font = m_font->GetThreadSafeView();
			return res;
		}

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override {
			if (!m_info->Codepoints.contains(codepoint))
				return nullptr;

			return m_font->GetBaseFont(codepoint);
		}
	};
}

#endif
