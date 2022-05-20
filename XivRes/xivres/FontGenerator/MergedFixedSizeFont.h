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
		struct InfoStruct {
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			float Size{};
			int Ascent{};
			int LineHeight{};
			MergedFontVerticalAlignment Alignment = MergedFontVerticalAlignment::Baseline;
		};

		std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> m_fonts;
		std::shared_ptr<const InfoStruct> m_info;

	public:
		MergedFixedSizeFont() = default;
		MergedFixedSizeFont(MergedFixedSizeFont&&) = default;
		MergedFixedSizeFont(const MergedFixedSizeFont&) = default;
		MergedFixedSizeFont& operator=(MergedFixedSizeFont&&) = default;
		MergedFixedSizeFont& operator=(const MergedFixedSizeFont&) = default;

		MergedFixedSizeFont(std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> fonts, MergedFontVerticalAlignment verticalAlignment = MergedFontVerticalAlignment::Baseline)
			: m_fonts(std::move(fonts)) {

			if (m_fonts.empty())
				throw std::invalid_argument("At least 1 font is required.");

			auto info = std::make_shared<InfoStruct>();
			info->Size = m_fonts.front()->GetSize();
			info->Ascent = m_fonts.front()->GetAscent();
			info->LineHeight = m_fonts.front()->GetLineHeight();
			info->Alignment = verticalAlignment;

			std::set<uint32_t> newCodepoints;
			for (const auto& font : m_fonts) {
				newCodepoints.clear();
				std::ranges::set_difference(font->GetAllCodepoints(), info->Codepoints, std::inserter(newCodepoints, newCodepoints.end()));
				info->Codepoints.insert(newCodepoints.begin(), newCodepoints.end());

				for (const auto& kerningPair : font->GetKerningPairs()) {
					if (newCodepoints.contains(kerningPair.first.first) && newCodepoints.contains(kerningPair.first.second))
						info->KerningPairs.emplace(kerningPair);
				}
			}

			m_info = std::move(info);
		}

		MergedFontVerticalAlignment GetComponentVerticalAlignment() const {
			return m_info->Alignment;
		}

		float GetSize() const override {
			return m_info->Size;
		}

		int GetAscent() const override {
			return m_info->Ascent;
		}

		int GetLineHeight() const override {
			return m_info->LineHeight;
		}

		const std::set<char32_t>& GetAllCodepoints() const override {
			return m_info->Codepoints;
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

		const std::map<std::pair<char32_t, char32_t>, int>& GetKerningPairs() const override {
			return m_info->KerningPairs;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			int kerningDistance = 0;
			if (auto it = m_info->KerningPairs.find(std::make_pair(left, right)); it != m_info->KerningPairs.end())
				kerningDistance = it->second;

			return gm.AdvanceX + kerningDistance;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, float gamma) const override {
			for (size_t i = 0; i < m_fonts.size(); i++) {
				if (m_fonts[i]->Draw(codepoint, pBuf, drawX, drawY + GetVerticalAdjustment(i), destWidth, destHeight, fgColor, bgColor, gamma))
					return true;
			}

			return false;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity, float gamma) const override {
			for (size_t i = 0; i < m_fonts.size(); i++) {
				if (m_fonts[i]->Draw(codepoint, pBuf, stride, drawX, drawY + GetVerticalAdjustment(i), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity, gamma))
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
			switch (m_info->Alignment) {
				case MergedFontVerticalAlignment::Top:
					return 0;
				case MergedFontVerticalAlignment::Middle:
					return 0 + (m_info->LineHeight - m_fonts[index]->GetLineHeight()) / 2;
				case MergedFontVerticalAlignment::Baseline:
					return 0 + m_info->Ascent - m_fonts[index]->GetAscent();
				case MergedFontVerticalAlignment::Bottom:
					return 0 + m_info->LineHeight - m_fonts[index]->GetLineHeight();
				default:
					throw std::runtime_error("Invalid alignment value set");
			}
		}
	};
}

#endif
