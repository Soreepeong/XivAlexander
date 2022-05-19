#ifndef _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {
	class CodepointLimitingFixedSizeFont : public IFixedSizeFont {
		std::shared_ptr<const IFixedSizeFont> m_pFont;
		std::shared_ptr<std::set<char32_t>> m_codepoints;
		std::shared_ptr<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		CodepointLimitingFixedSizeFont(std::shared_ptr<const IFixedSizeFont> pFont, std::set<char32_t> codepoints)
			: m_pFont(std::move(pFont))
			, m_codepoints(std::make_shared<std::set<char32_t>>())
			, m_kerningPairs(std::make_shared<std::map<std::pair<char32_t, char32_t>, int>>(m_pFont->GetKerningPairs())) {

			std::ranges::set_intersection(codepoints, m_pFont->GetAllCodepoints(), std::inserter(*m_codepoints, m_codepoints->begin()));
			;
			for (auto it = m_kerningPairs->begin(); it != m_kerningPairs->end(); ) {
				if (m_codepoints->contains(it->first.first) && m_codepoints->contains(it->first.second))
					++it;
				else
					it = m_kerningPairs->erase(it);
			}
		}

		CodepointLimitingFixedSizeFont(const CodepointLimitingFixedSizeFont& r) = default;

		CodepointLimitingFixedSizeFont(CodepointLimitingFixedSizeFont&& r) = default;

		CodepointLimitingFixedSizeFont& operator=(const CodepointLimitingFixedSizeFont& r) = default;

		CodepointLimitingFixedSizeFont& operator=(CodepointLimitingFixedSizeFont&& r) = default;

		int GetHorizontalOffset() const override {
			return m_pFont->GetHorizontalOffset();
		}

		void SetHorizontalOffset(int offset) override {
			throw std::runtime_error("FixedSizeFontConstView does not support changing horizontal offset.");
		}

		float GetSize() const override {
			return m_pFont->GetSize();
		}

		int GetAscent() const override {
			return m_pFont->GetAscent();
		}

		int GetLineHeight() const override {
			return m_pFont->GetLineHeight();
		}

		size_t GetCodepointCount() const override {
			return m_codepoints->size();
		}

		std::set<char32_t> GetAllCodepoints() const override {
			return *m_codepoints;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			if (!m_codepoints->contains(codepoint)) {
				gm.Clear();
				return false;
			}

			return m_pFont->GetGlyphMetrics(codepoint, gm);
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			if (!m_codepoints->contains(c))
				return nullptr;

			return m_pFont->GetGlyphUniqid(c);
		}

		size_t GetKerningEntryCount() const override {
			return m_kerningPairs->size();
		}

		std::map<std::pair<char32_t, char32_t>, int> GetKerningPairs() const override {
			return *m_kerningPairs;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			int kerningDistance = 0;
			if (auto it = m_kerningPairs->find(std::make_pair(left, right)); it != m_kerningPairs->end())
				kerningDistance = it->second;

			return gm.AdvanceX + kerningDistance;
		}

		const std::array<uint8_t, 256>& GetGammaTable() const override {
			return LinearGammaTable;
		}

		void SetGammaTable(const std::array<uint8_t, 256>& gammaTable) override {
			throw std::runtime_error("CodepointLimitingFixedSizeFont does not support changing gamma table.");
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			if (!m_codepoints->contains(codepoint))
				return false;

			return m_pFont->Draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor);
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			if (!m_codepoints->contains(codepoint))
				return false;

			return m_pFont->Draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			auto res = std::make_shared<CodepointLimitingFixedSizeFont>(*this);
			res->m_pFont = m_pFont->GetThreadSafeView();
			return res;
		}
	};
}

#endif
