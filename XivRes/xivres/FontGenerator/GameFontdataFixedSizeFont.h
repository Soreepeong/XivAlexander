#ifndef _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_

#include <vector>

#include "../GameReader.h"
#include "../TextureStream.h"

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"

namespace XivRes::FontGenerator {
	enum class GameFontFamily {
		AXIS,
		Jupiter,
		JupiterN,
		MiedingerMid,
		Meidinger,
		TrumpGothic,
		ChnAXIS,
		KrnAXIS,
	};

	struct GameFontdataDefinition {
		const char* Path;
		const char* Name;
		const char* Family;
		float Size;
	};

	class GameFontdataFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::shared_ptr<const FontdataStream> Font;
			std::string FamilyName;
			std::string SubfamilyName;
			std::vector<std::shared_ptr<MemoryMipmapStream>> Mipmaps;
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			std::vector<uint8_t> GammaTable;
		};

		std::shared_ptr<const InfoStruct> m_info;

	public:
		GameFontdataFixedSizeFont(std::shared_ptr<const FontdataStream> stream, std::vector<std::shared_ptr<MemoryMipmapStream>> mipmapStreams, std::string familyName, std::string subfamilyName) {
			for (const auto& mipmapStream : mipmapStreams) {
				if (mipmapStream->Type != TextureFormat::A8R8G8B8)
					throw std::invalid_argument("All mipmap streams must be in A8R8G8B8 format.");
			}

			auto info = std::make_shared<InfoStruct>();
			info->Font = std::move(stream);
			info->FamilyName = std::move(familyName);
			info->SubfamilyName = std::move(subfamilyName);
			info->Mipmaps = std::move(mipmapStreams);
			info->GammaTable = Internal::BitmapCopy::CreateGammaTable(1.f);

			for (const auto& entry : info->Font->GetFontTableEntries())
				info->Codepoints.insert(info->Codepoints.end(), entry.Char());

			for (const auto& entry : info->Font->GetKerningEntries())
				info->KerningPairs.emplace_hint(info->KerningPairs.end(), std::make_pair(entry.Left(), entry.Right()), entry.RightOffset);

			m_info = std::move(info);
		}

		GameFontdataFixedSizeFont() = default;
		GameFontdataFixedSizeFont(GameFontdataFixedSizeFont&&) = default;
		GameFontdataFixedSizeFont(const GameFontdataFixedSizeFont& r) = default;
		GameFontdataFixedSizeFont& operator=(GameFontdataFixedSizeFont&&) = default;
		GameFontdataFixedSizeFont& operator=(const GameFontdataFixedSizeFont&) = default;

		std::string GetFamilyName() const override {
			return m_info->FamilyName;
		}

		std::string GetSubfamilyName() const override {
			return m_info->SubfamilyName;
		}

		float GetSize() const override {
			return m_info->Font->Size();
		}

		int GetAscent() const override {
			return m_info->Font->Ascent();
		}

		int GetLineHeight() const override {
			return m_info->Font->LineHeight();
		}

		const std::set<char32_t>& GetAllCodepoints() const override {
			return m_info->Codepoints;
		}

		const void* GetBaseFontGlyphUniqid(char32_t c) const override {
			return m_info->Font->GetFontEntry(c);
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			const auto p = m_info->Font->GetFontEntry(codepoint);
			if (!p)
				return false;

			gm = GlyphMetricsFromEntry(p);
			return true;
		}

		const std::map<std::pair<char32_t, char32_t>, int>& GetAllKerningPairs() const override {
			return m_info->KerningPairs;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			return gm.AdvanceX + m_info->Font->GetKerningDistance(left, right);
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			const auto pEntry = m_info->Font->GetFontEntry(codepoint);
			if (!pEntry)
				return false;

			auto src = GlyphMetrics{ *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->TextureFileIndex());
			const auto planeIndex = FontdataGlyphEntry::ChannelMap[pEntry->TexturePlaneIndex()];
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			Internal::BitmapCopy::ToRGBA8888()
				.From(&mipmapStream.View<uint8_t>()[planeIndex], mipmapStream.Width, mipmapStream.Height, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithBackgroundColor(bgColor)
				.WithGammaTable(m_info->GammaTable)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			const auto pEntry = m_info->Font->GetFontEntry(codepoint);
			if (!pEntry)
				return false;

			auto src = GlyphMetrics{ *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->TextureFileIndex());
			const auto planeIndex = FontdataGlyphEntry::ChannelMap[pEntry->TexturePlaneIndex()];
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			Internal::BitmapCopy::ToL8()
				.From(&mipmapStream.View<uint8_t>()[planeIndex], mipmapStream.Width, mipmapStream.Height, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithForegroundOpacity(fgOpacity)
				.WithBackgroundColor(bgColor)
				.WithBackgroundOpacity(bgOpacity)
				.WithGammaTable(m_info->GammaTable)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return std::make_shared<GameFontdataFixedSizeFont>(*this);
		}

		const IFixedSizeFont* GetBaseFont(char32_t codepoint) const override {
			return this;
		}

	private:
		GlyphMetrics GlyphMetricsFromEntry(const FontdataGlyphEntry* pEntry, int x = 0, int y = 0) const {
			GlyphMetrics src{
				.X1 = x,
				.Y1 = y + pEntry->CurrentOffsetY,
				.X2 = src.X1 + pEntry->BoundingWidth,
				.Y2 = src.Y1 + pEntry->BoundingHeight,
				.AdvanceX = pEntry->BoundingWidth + pEntry->NextOffsetX,
			};
			return src;
		}
	};

	class GameFontdataSet {
		XivRes::GameFontType m_gameFontType;
		std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> m_data;

	public:
		GameFontdataSet() = default;
		GameFontdataSet(GameFontdataSet&&) = default;
		GameFontdataSet(const GameFontdataSet&) = default;
		GameFontdataSet& operator=(GameFontdataSet&&) = default;
		GameFontdataSet& operator=(const GameFontdataSet&) = default;

		GameFontdataSet(XivRes::GameFontType gameFontType, std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> data)
			: m_gameFontType(gameFontType)
			, m_data(std::move(data)) {}

		std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont> operator[](size_t i) const {
			return m_data[i];
		}

		std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont> GetFont(size_t i) const {
			return m_data[i];
		}

		std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont> GetFont(GameFontFamily family, float size) const {
			std::vector<size_t> candidates;
			candidates.reserve(5);

			for (size_t i = 0; i < m_data.size(); i++) {
				const auto name = m_data[i]->GetFamilyName();
				if (family == GameFontFamily::AXIS && name == "AXIS")
					candidates.push_back(i);
				else if (family == GameFontFamily::Jupiter && name == "Jupiter")
					candidates.push_back(i);
				else if (family == GameFontFamily::JupiterN && name == "JupiterN")
					candidates.push_back(i);
				else if (family == GameFontFamily::Meidinger && name == "Meidinger")
					candidates.push_back(i);
				else if (family == GameFontFamily::MiedingerMid && name == "MiedingerMid")
					candidates.push_back(i);
				else if (family == GameFontFamily::TrumpGothic && name == "TrumpGothic")
					candidates.push_back(i);
				else if (family == GameFontFamily::ChnAXIS && name == "ChnAXIS")
					candidates.push_back(i);
				else if (family == GameFontFamily::KrnAXIS && name == "KrnAXIS")
					candidates.push_back(i);
			}

			if (candidates.empty())
				return {};

			std::sort(candidates.begin(), candidates.end(), [this, size](const auto& l, const auto& r) {
				return std::fabsf(size - m_data[l]->GetSize()) < std::fabsf(size - m_data[r]->GetSize());
			});

			return m_data[candidates[0]];
		}

		size_t Count() const {
			return m_data.size();
		}

		operator bool() const {
			return !m_data.empty();
		}
	};

	static std::span<const FontGenerator::GameFontdataDefinition> GetFontDefinition(GameFontType fontType = GameFontType::font) {
		static const FontGenerator::GameFontdataDefinition fdtListFont[]{
			{"common/font/AXIS_96.fdt", "AXIS", "Regular", 9.6f},
			{"common/font/AXIS_12.fdt", "AXIS", "Regular", 12.f},
			{"common/font/AXIS_14.fdt", "AXIS", "Regular", 14.f},
			{"common/font/AXIS_18.fdt", "AXIS", "Regular", 18.f},
			{"common/font/AXIS_36.fdt", "AXIS", "Regular", 36.f},
			{"common/font/Jupiter_16.fdt", "Jupiter", "Regular", 16.f},
			{"common/font/Jupiter_20.fdt", "Jupiter", "Regular", 20.f},
			{"common/font/Jupiter_23.fdt", "Jupiter", "Regular", 23.f},
			{"common/font/Jupiter_45.fdt", "JupiterN", "Regular", 45.f},
			{"common/font/Jupiter_46.fdt", "Jupiter", "Regular", 46.f},
			{"common/font/Jupiter_90.fdt", "JupiterN", "Regular", 90.f},
			{"common/font/Meidinger_16.fdt", "Meidinger", "Regular", 16.f},
			{"common/font/Meidinger_20.fdt", "Meidinger", "Regular", 20.f},
			{"common/font/Meidinger_40.fdt", "Meidinger", "Regular", 40.f},
			{"common/font/MiedingerMid_10.fdt", "MiedingerMid", "Medium", 10.f},
			{"common/font/MiedingerMid_12.fdt", "MiedingerMid", "Medium", 12.f},
			{"common/font/MiedingerMid_14.fdt", "MiedingerMid", "Medium", 14.f},
			{"common/font/MiedingerMid_18.fdt", "MiedingerMid", "Medium", 18.f},
			{"common/font/MiedingerMid_36.fdt", "MiedingerMid", "Medium", 36.f},
			{"common/font/TrumpGothic_184.fdt", "TrumpGothic", "Regular", 18.4f},
			{"common/font/TrumpGothic_23.fdt", "TrumpGothic", "Regular", 23.f},
			{"common/font/TrumpGothic_34.fdt", "TrumpGothic", "Regular", 34.f},
			{"common/font/TrumpGothic_68.fdt", "TrumpGothic", "Regular", 68.f},
		};

		static const FontGenerator::GameFontdataDefinition fdtListFontLobby[]{
			{"common/font/AXIS_12_lobby.fdt", "AXIS", "Regular", 12.f},
			{"common/font/AXIS_14_lobby.fdt", "AXIS", "Regular", 14.f},
			{"common/font/AXIS_18_lobby.fdt", "AXIS", "Regular", 18.f},
			{"common/font/AXIS_36_lobby.fdt", "AXIS", "Regular", 36.f},
			{"common/font/Jupiter_16_lobby.fdt", "Jupiter", "Regular", 16.f},
			{"common/font/Jupiter_20_lobby.fdt", "Jupiter", "Regular", 20.f},
			{"common/font/Jupiter_23_lobby.fdt", "Jupiter", "Regular", 23.f},
			{"common/font/Jupiter_45_lobby.fdt", "JupiterN", "Regular", 45.f},
			{"common/font/Jupiter_46_lobby.fdt", "Jupiter", "Regular", 46.f},
			{"common/font/Jupiter_90_lobby.fdt", "JupiterN", "Regular", 90.f},
			{"common/font/Meidinger_16_lobby.fdt", "Meidinger", "Regular", 16.f},
			{"common/font/Meidinger_20_lobby.fdt", "Meidinger", "Regular", 20.f},
			{"common/font/Meidinger_40_lobby.fdt", "Meidinger", "Regular", 40.f},
			{"common/font/MiedingerMid_10_lobby.fdt", "MiedingerMid", "Medium", 10.f},
			{"common/font/MiedingerMid_12_lobby.fdt", "MiedingerMid", "Medium", 12.f},
			{"common/font/MiedingerMid_14_lobby.fdt", "MiedingerMid", "Medium", 14.f},
			{"common/font/MiedingerMid_18_lobby.fdt", "MiedingerMid", "Medium", 18.f},
			{"common/font/MiedingerMid_36_lobby.fdt", "MiedingerMid", "Medium", 36.f},
			{"common/font/TrumpGothic_184_lobby.fdt", "TrumpGothic", "Regular", 18.4f},
			{"common/font/TrumpGothic_23_lobby.fdt", "TrumpGothic", "Regular", 23.f},
			{"common/font/TrumpGothic_34_lobby.fdt", "TrumpGothic", "Regular", 34.f},
			{"common/font/TrumpGothic_68_lobby.fdt", "TrumpGothic", "Regular", 68.f},
		};

		static const FontGenerator::GameFontdataDefinition fdtListKrnAxis[]{
			{"common/font/KrnAXIS_120.fdt", "KrnAXIS", "Regular", 12.f},
			{"common/font/KrnAXIS_140.fdt", "KrnAXIS", "Regular", 14.f},
			{"common/font/KrnAXIS_180.fdt", "KrnAXIS", "Regular", 18.f},
			{"common/font/KrnAXIS_360.fdt", "KrnAXIS", "Regular", 36.f},
		};

		static const FontGenerator::GameFontdataDefinition fdtListChnAxis[]{
			{"common/font/ChnAXIS_120.fdt", "ChnAXIS", "Regular", 12.f},
			{"common/font/ChnAXIS_140.fdt", "ChnAXIS", "Regular", 14.f},
			{"common/font/ChnAXIS_180.fdt", "ChnAXIS", "Regular", 18.f},
			{"common/font/ChnAXIS_360.fdt", "ChnAXIS", "Regular", 36.f},
		};

		switch (fontType) {
			case XivRes::GameFontType::font:
				return { fdtListFont };

			case XivRes::GameFontType::font_lobby:
				return { fdtListFontLobby };

			case XivRes::GameFontType::chn_axis:
				return { fdtListChnAxis };

			case XivRes::GameFontType::krn_axis:
				return {fdtListKrnAxis};

			default:
				return {};
		}
	}

	static const char* GetFontTexFilenameFormat(GameFontType fontType = GameFontType::font) {
		switch (fontType) {
			case XivRes::GameFontType::font: return "common/font/font{}.tex";
			case XivRes::GameFontType::font_lobby: return "common/font/font_lobby{}.tex";
			case XivRes::GameFontType::chn_axis: return "common/font/font_chn_{}.tex";
			case XivRes::GameFontType::krn_axis: return "common/font/font_krn_{}.tex";
			default: return nullptr;
		}
	}
}

inline XivRes::FontGenerator::GameFontdataSet XivRes::GameReader::GetFonts(XivRes::GameFontType gameFontType, std::span<const FontGenerator::GameFontdataDefinition> gameFontdataDefinitions, const char* pcszTexturePathPattern) const {
	std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>> textures;
	try {
		for (int i = 1; ; i++)
			textures.emplace_back(XivRes::MemoryMipmapStream::AsARGB8888(*XivRes::TextureStream(GetFileStream(std::format(pcszTexturePathPattern, i))).GetMipmap(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}

	std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> fonts;
	fonts.reserve(gameFontdataDefinitions.size());
	for (const auto& def : gameFontdataDefinitions) {
		fonts.emplace_back(std::make_shared<FontGenerator::GameFontdataFixedSizeFont>(
			std::make_shared<FontdataStream>(*GetFileStream(def.Path)),
			textures,
			def.Name,
			def.Family));
	}

	return { gameFontType, fonts };
}

inline XivRes::FontGenerator::GameFontdataSet XivRes::GameReader::GetFonts(XivRes::GameFontType fontType) const {
	return GetFonts(fontType, XivRes::FontGenerator::GetFontDefinition(fontType), XivRes::FontGenerator::GetFontTexFilenameFormat(fontType));
}

#endif
