#ifndef _XIVRES_FONTGENERATOR_MERGEDFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_MERGEDFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {
	enum class MergedFontVerticalAlignment {
		Top,
		Middle,
		Baseline,
		Bottom,
	};

	class MergedFixedSizeFont : public IFixedSizeFont {
		struct MergeInfo {
			std::set<char32_t> m_codepoints;
			std::map<std::pair<char32_t, char32_t>, int> m_kerningPairs;
			float m_fSize{};
			int m_nAscent{};
			int m_nLineHeight{};
			MergedFontVerticalAlignment m_alignment = MergedFontVerticalAlignment::Baseline;
			std::vector<int> m_verticalAdjustments;
		};

		std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> m_fonts;
		std::shared_ptr<MergeInfo> m_info;

	public:
		MergedFixedSizeFont(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> fonts)
			: m_fonts(std::move(fonts))
			, m_info(std::make_shared<MergeInfo>()) {

			if (m_fonts.empty())
				throw std::invalid_argument("At least 1 font is required.");

			m_info->m_fSize = m_fonts.front()->GetSize();
			m_info->m_nAscent = m_fonts.front()->GetAscent();
			m_info->m_nLineHeight = m_fonts.front()->GetLineHeight();

			std::set<uint32_t> newCodepoints;
			for (const auto& font : m_fonts) {
				newCodepoints.clear();
				std::ranges::set_difference(font->GetAllCodepoints(), m_info->m_codepoints, std::inserter(newCodepoints, newCodepoints.end()));
				m_info->m_codepoints.insert(newCodepoints.begin(), newCodepoints.end());

				for (const auto& kerningPair : font->GetKerningPairs()) {
					if (newCodepoints.contains(kerningPair.first.first) && newCodepoints.contains(kerningPair.first.second))
						m_info->m_kerningPairs.emplace(kerningPair);
				}
			}

			m_info->m_verticalAdjustments.resize(m_fonts.size());
		}

		MergedFixedSizeFont(const MergedFixedSizeFont& r) = default;

		MergedFixedSizeFont(MergedFixedSizeFont && r) = default;

		MergedFixedSizeFont& operator=(const MergedFixedSizeFont & r) = default;

		MergedFixedSizeFont& operator=(MergedFixedSizeFont && r) = default;

		int GetIndividualVerticalAdjustment(size_t index) const {
			return m_info->m_verticalAdjustments.at(index);
		}

		void SetIndividualVerticalAdjustment(size_t index, int adjustment) {
			m_info->m_verticalAdjustments.at(index) = adjustment;
		}

		MergedFontVerticalAlignment GetComponentVerticalAlignment() const {
			return m_info->m_alignment;
		}

		void SetComponentVerticalAlignment(MergedFontVerticalAlignment newAlignment) {
			m_info->m_alignment = newAlignment;
		}

		float GetSize() const override {
			return m_info->m_fSize;
		}

		int GetAscent() const override {
			return m_info->m_nAscent;
		}

		int GetLineHeight() const override {
			return m_info->m_nLineHeight;
		}

		size_t GetCodepointCount() const override {
			return m_info->m_codepoints.size();
		}

		std::set<char32_t> GetAllCodepoints() const override {
			return m_info->m_codepoints;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			for (size_t i = 0; i < m_fonts.size(); i++) {
				if (m_fonts[i]->GetGlyphMetrics(codepoint, gm)) {
					gm.Translate(0, GetVerticalAdjustment(i));
					return true;
				}
			}
			return false;
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			for (size_t i = 0; i < m_fonts.size(); i++) {
				if (const auto uniqid = m_fonts[i]->GetGlyphUniqid(c))
					return uniqid;
			}

			return nullptr;
		}

		size_t GetKerningEntryCount() const override {
			return m_info->m_kerningPairs.size();
		}

		std::map<std::pair<char32_t, char32_t>, int> GetKerningPairs() const override {
			return m_info->m_kerningPairs;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			int kerningDistance = 0;
			if (auto it = m_info->m_kerningPairs.find(std::make_pair(left, right)); it != m_info->m_kerningPairs.end())
				kerningDistance = it->second;

			return gm.AdvanceX + kerningDistance;
		}

		const std::array<uint8_t, 256>& GetGammaTable() const override {
			return LinearGammaTable;
		}

		void SetGammaTable(const std::array<uint8_t, 256>& gammaTable) override {
			throw std::runtime_error("MergedFixedSizeFont does not support changing gamma table.");
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			for (size_t i = 0; i < m_fonts.size(); i++) {
				if (m_fonts[i]->Draw(codepoint, pBuf, drawX, drawY + GetVerticalAdjustment(i), destWidth, destHeight, fgColor, bgColor))
					return true;
			}

			return false;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			for (size_t i = 0; i < m_fonts.size(); i++) {
				if (m_fonts[i]->Draw(codepoint, pBuf, stride, drawX, drawY + GetVerticalAdjustment(i), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity))
					return true;
			}

			return false;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			auto res = std::make_shared<MergedFixedSizeFont>(*this);
			res->m_fonts.clear();
			for (const auto& font : m_fonts)
				res->m_fonts.emplace_back(font->GetThreadSafeView());
			return res;
		}

	private:
		int GetVerticalAdjustment(size_t index) const {
			switch (m_info->m_alignment) {
				case MergedFontVerticalAlignment::Top:
					return m_info->m_verticalAdjustments[index];
				case MergedFontVerticalAlignment::Middle:
					return m_info->m_verticalAdjustments[index] + (m_info->m_nLineHeight - m_fonts[index]->GetLineHeight()) / 2;
				case MergedFontVerticalAlignment::Baseline:
					return m_info->m_verticalAdjustments[index] + m_info->m_nAscent - m_fonts[index]->GetAscent();
				case MergedFontVerticalAlignment::Bottom:
					return m_info->m_verticalAdjustments[index] + m_info->m_nLineHeight - m_fonts[index]->GetLineHeight();
				default:
					throw std::runtime_error("Invalid alignment value set");
			}
		}
	};
}

#endif
