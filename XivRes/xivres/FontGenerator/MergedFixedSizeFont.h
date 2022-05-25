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

	class MergedFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::set<char32_t> Codepoints;
			std::map<char32_t, IFixedSizeFont*> UsedFonts;
			float Size{};
			int Ascent{};
			int LineHeight{};
			MergedFontVerticalAlignment Alignment = MergedFontVerticalAlignment::Baseline;
		};

		std::vector<std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>> m_fonts;
		std::shared_ptr<const InfoStruct> m_info;
		mutable std::optional<std::map<std::pair<char32_t, char32_t>, int>> m_kerningPairs;

	public:
		MergedFixedSizeFont() = default;
		MergedFixedSizeFont(MergedFixedSizeFont&&) = default;
		MergedFixedSizeFont(const MergedFixedSizeFont&) = default;
		MergedFixedSizeFont& operator=(MergedFixedSizeFont&&) = default;
		MergedFixedSizeFont& operator=(const MergedFixedSizeFont&) = default;

		MergedFixedSizeFont(std::vector<std::pair<std::shared_ptr<IFixedSizeFont>, bool>> fonts, MergedFontVerticalAlignment verticalAlignment = MergedFontVerticalAlignment::Baseline) {
			auto info = std::make_shared<InfoStruct>();
			if (fonts.empty())
				fonts.emplace_back(std::make_shared<EmptyFixedSizeFont>(), false);

			info->Alignment = verticalAlignment;
			info->Size = fonts.front().first->GetSize();
			info->Ascent = fonts.front().first->GetAscent();
			info->LineHeight = fonts.front().first->GetLineHeight();

			for (auto& [font, overwrite] : fonts) {
				for (const auto c : font->GetAllCodepoints()) {
					if (overwrite) {
						info->UsedFonts.insert_or_assign(c, font.get());
						info->Codepoints.insert(c);
					} else if (info->UsedFonts.emplace(c, font.get()).second)
						info->Codepoints.insert(c);
				}

				m_fonts.emplace_back(std::move(font));
			}

			m_info = std::move(info);
		}

		MergedFontVerticalAlignment GetComponentVerticalAlignment() const {
			return m_info->Alignment;
		}

		std::string GetFamilyName() const override {
			return "Merged";
		}

		std::string GetSubfamilyName() const override {
			return {};
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

		const void* GetBaseFontGlyphUniqid(char32_t c) const override {
			if (const auto it = m_info->UsedFonts.find(c); it != m_info->UsedFonts.end())
				return it->second->GetBaseFontGlyphUniqid(c);

			return nullptr;
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end()) {
				if (!it->second->GetGlyphMetrics(codepoint, gm))
					return false;

				gm.Translate(0, GetVerticalAdjustment(*m_info, *it->second));
				return true;
			}

			return false;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			if (m_kerningPairs)
				return *m_kerningPairs;

			std::map<IFixedSizeFont*, std::set<char32_t>> charsPerFonts;
			for (const auto& [c, f] : m_info->UsedFonts)
				charsPerFonts[f].insert(c);

			m_kerningPairs.emplace();
			for (const auto& [font, chars] : charsPerFonts) {
				for (const auto& kerningPair : font->GetAllKerningPairs()) {
					if (chars.contains(kerningPair.first.first) && chars.contains(kerningPair.first.second))
						m_kerningPairs->emplace(kerningPair);
				}
			}

			return *m_kerningPairs;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
				return it->second->Draw(codepoint, pBuf, drawX, drawY + GetVerticalAdjustment(*m_info, *it->second), destWidth, destHeight, fgColor, bgColor);

			return false;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
				return it->second->Draw(codepoint, pBuf, stride, drawX, drawY + GetVerticalAdjustment(*m_info, *it->second), destWidth, destHeight, fgColor, bgColor, fgOpacity, bgOpacity);

			return false;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			auto res = std::make_shared<MergedFixedSizeFont>(*this);
			res->m_fonts.clear();
			for (const auto& font : m_fonts)
				res->m_fonts.emplace_back(font->GetThreadSafeView());
			return res;
		}

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override {
			if (const auto it = m_info->UsedFonts.find(codepoint); it != m_info->UsedFonts.end())
				return it->second->GetBaseFont(codepoint);

			return nullptr;
		}

	private:
		static int GetVerticalAdjustment(const InfoStruct& info, const XivRes::FontGenerator::IFixedSizeFont& font) {
			switch (info.Alignment) {
				case MergedFontVerticalAlignment::Top:
					return 0;
				case MergedFontVerticalAlignment::Middle:
					return 0 + (info.LineHeight - font.GetLineHeight()) / 2;
				case MergedFontVerticalAlignment::Baseline:
					return 0 + info.Ascent - font.GetAscent();
				case MergedFontVerticalAlignment::Bottom:
					return 0 + info.LineHeight - font.GetLineHeight();
				default:
					throw std::runtime_error("Invalid alignment value set");
			}
		}
	};
}

#endif
