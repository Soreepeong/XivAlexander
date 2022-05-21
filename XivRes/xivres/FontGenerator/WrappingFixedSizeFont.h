#ifndef _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_CODEPOINTLIMITINGFIXEDSIZEFONT_H_

#include "IFixedSizeFont.h"

namespace XivRes::FontGenerator {
	struct WrapModifiers {
		std::optional<std::vector<char32_t>> Codepoints;
		int OffsetAdvanceX = 0;
		int OffsetCurrentY = 0;
		float Gamma = 1.f;
	};

	class WrappingFixedSizeFont : public IFixedSizeFont {
		struct InfoStruct {
			std::optional<std::set<char32_t>> Codepoints;
			std::optional<std::map<std::pair<char32_t, char32_t>, int>> KerningPairs;
			int OffsetAdvanceX{};
			int OffsetCurrentY{};
			float Gamma{};
		};

		std::shared_ptr<const IFixedSizeFont> m_font;
		std::shared_ptr<const InfoStruct> m_info;

	public:
		WrappingFixedSizeFont(std::shared_ptr<const IFixedSizeFont> font, const WrapModifiers& wrapModifiers)
			: m_font(std::move(font)) {

			auto info = std::make_shared<InfoStruct>();
			if (wrapModifiers.Codepoints) {
				info->Codepoints.emplace();
				info->KerningPairs.emplace();
				std::ranges::set_intersection(*wrapModifiers.Codepoints, m_font->GetAllCodepoints(), std::inserter(*info->Codepoints, info->Codepoints->begin()));
				for (auto it = info->KerningPairs->begin(); it != info->KerningPairs->end(); ) {
					if (info->Codepoints->contains(it->first.first) && info->Codepoints->contains(it->first.second))
						++it;
					else
						it = info->KerningPairs->erase(it);
				}
			}

			info->OffsetAdvanceX = wrapModifiers.OffsetAdvanceX;
			info->OffsetCurrentY = wrapModifiers.OffsetCurrentY;
			info->Gamma = wrapModifiers.Gamma;

			m_info = std::move(info);
		}

		WrappingFixedSizeFont() = default;
		WrappingFixedSizeFont(const WrappingFixedSizeFont& r) = default;
		WrappingFixedSizeFont(WrappingFixedSizeFont&& r) = default;
		WrappingFixedSizeFont& operator=(const WrappingFixedSizeFont& r) = default;
		WrappingFixedSizeFont& operator=(WrappingFixedSizeFont&& r) = default;

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
			return m_info->Codepoints.value_or(m_font->GetAllCodepoints());
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			if (m_info->Codepoints && !m_info->Codepoints->contains(codepoint)) {
				gm.Clear();
				return false;
			}

			return m_font->GetGlyphMetrics(codepoint, gm);
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			if (m_info->Codepoints && !m_info->Codepoints->contains(c))
				return nullptr;

			return m_font->GetGlyphUniqid(c);
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			return m_info->KerningPairs.value_or(m_font->GetAllKerningPairs());
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			const auto& kerningPairs = m_info->KerningPairs.value_or(m_font->GetAllKerningPairs());
			int kerningDistance = 0;
			if (auto it = kerningPairs.find(std::make_pair(left, right)); it != kerningPairs.end())
				kerningDistance = it->second;

			return gm.AdvanceX + kerningDistance;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, float gamma) const override {
			if (m_info->Codepoints && !m_info->Codepoints->contains(codepoint))
				return false;

			return m_font->Draw(codepoint, pBuf, drawX, drawY, destWidth, destHeight, fgColor, bgColor, gamma);
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity, float gamma) const override {
			if (m_info->Codepoints && !m_info->Codepoints->contains(codepoint))
				return false;

			return m_font->Draw(codepoint, pBuf, stride, drawX, drawY, destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity, gamma);
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			auto res = std::make_shared<WrappingFixedSizeFont>(*this);
			res->m_font = m_font->GetThreadSafeView();
			return res;
		}
	};
}

#endif
