#ifndef _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_

#include <vector>

#include "../GameReader.h"
#include "../TextureStream.h"

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"

namespace XivRes::FontGenerator {
	class GameFontdataSet {
		const XivRes::GameFontType m_gameFontType;
		const std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> m_data;

	public:
		GameFontdataSet(XivRes::GameFontType gameFontType, std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> data)
			: m_gameFontType(gameFontType)
			, m_data(std::move(data)) {
		}

		std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont> operator[](size_t i) const {
			return m_data[i];
		}

		size_t Count() const {
			return m_data.size();
		}
	};

	class GameFontdataFixedSizeFont : public DefaultAbstractFixedSizeFont {
		struct InfoStruct {
			std::shared_ptr<const FontdataStream> Font;
			std::vector<std::shared_ptr<MemoryMipmapStream>> Mipmaps;
			std::set<char32_t> Codepoints;
			std::map<std::pair<char32_t, char32_t>, int> KerningPairs;
			int HorizontalOffset = 0;
		};

		std::shared_ptr<const InfoStruct> m_info;

	public:
		GameFontdataFixedSizeFont(std::shared_ptr<const FontdataStream> stream, std::vector<std::shared_ptr<MemoryMipmapStream>> mipmapStreams) {
			for (const auto& mipmapStream : mipmapStreams) {
				if (mipmapStream->Type != TextureFormat::A8R8G8B8)
					throw std::invalid_argument("All mipmap streams must be in A8R8G8B8 format.");
			}

			auto info = std::make_shared<InfoStruct>();
			info->Font = std::move(stream);
			info->Mipmaps = std::move(mipmapStreams);

			for (const auto& entry : info->Font->GetFontTableEntries())
				info->Codepoints.insert(info->Codepoints.end(), entry.Char());

			for (const auto& entry : info->Font->GetKerningEntries())
				info->KerningPairs.emplace_hint(info->KerningPairs.end(), std::make_pair(entry.Left(), entry.Right()), entry.RightOffset);

			if (const auto pEntry = info->Font->GetFontEntry(U'_'))
				info->HorizontalOffset = -pEntry->NextOffsetX;
			else if (const auto pEntry = info->Font->GetFontEntry(U'0'))
				info->HorizontalOffset = -pEntry->NextOffsetX;

			m_info = std::move(info);
		}

		GameFontdataFixedSizeFont() = default;
		GameFontdataFixedSizeFont(GameFontdataFixedSizeFont&&) = default;
		GameFontdataFixedSizeFont(const GameFontdataFixedSizeFont & r) = default;
		GameFontdataFixedSizeFont& operator=(GameFontdataFixedSizeFont&&) = default;
		GameFontdataFixedSizeFont& operator=(const GameFontdataFixedSizeFont&) = default;

		int GetHorizontalOffset() const {
			return m_info->HorizontalOffset;
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

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			const auto pEntry = m_info->Font->GetFontEntry(codepoint);
			if (!pEntry) {
				gm.Clear();
				return false;
			}

			gm = GlyphMetricsFromEntry(pEntry);
			return true;
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			return m_info->Font->GetFontEntry(c);
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

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor, float gamma) const override {
			const auto pEntry = m_info->Font->GetFontEntry(codepoint);
			if (!pEntry)
				return false;

			auto src = GlyphMetrics{ false, *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->TextureFileIndex());
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			Internal::BitmapCopy::ToRGBA8888()
				.From(&mipmapStream.View<uint8_t>()[3 - pEntry->TexturePlaneIndex()], mipmapStream.Width, mipmapStream.Height, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithBackgroundColor(bgColor)
				.WithGamma(gamma)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity, float gamma) const override {
			const auto pEntry = m_info->Font->GetFontEntry(codepoint);
			if (!pEntry)
				return false;

			auto src = GlyphMetrics{ false, *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_info->Mipmaps.at(pEntry->TextureFileIndex());
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			Internal::BitmapCopy::ToL8()
				.From(&mipmapStream.View<uint8_t>()[3 - pEntry->TexturePlaneIndex()], mipmapStream.Width, mipmapStream.Height, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithForegroundOpacity(fgOpacity)
				.WithBackgroundColor(bgColor)
				.WithBackgroundOpacity(bgOpacity)
				.WithGamma(gamma)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		std::shared_ptr<IFixedSizeFont> GetThreadSafeView() const override {
			return std::make_shared<GameFontdataFixedSizeFont>(*this);
		}

	private:
		GlyphMetrics GlyphMetricsFromEntry(const FontdataGlyphEntry* pEntry, int x = 0, int y = 0) const {
			GlyphMetrics src{
				.Empty = false,
				.X1 = x - m_info->HorizontalOffset,
				.Y1 = y + pEntry->CurrentOffsetY,
				.X2 = src.X1 + pEntry->BoundingWidth,
				.Y2 = src.Y1 + pEntry->BoundingHeight,
				.AdvanceX = pEntry->BoundingWidth + pEntry->NextOffsetX,
			};
			return src;
		}
	};
}

inline XivRes::FontGenerator::GameFontdataSet XivRes::GameReader::GetFonts(XivRes::GameFontType gameFontType, const char* const* ppcszFontdataPath, const char* pcszTexturePathPattern) const {
	std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>> textures;
	try {
		for (int i = 1; ; i++)
			textures.emplace_back(XivRes::MemoryMipmapStream::AsARGB8888(*XivRes::TextureStream(GetFileStream(std::format(pcszTexturePathPattern, i))).GetMipmap(0, 0)));
	} catch (const std::out_of_range&) {
		// do nothing
	}

	std::vector<std::shared_ptr<XivRes::FontGenerator::GameFontdataFixedSizeFont>> fonts;
	while (*ppcszFontdataPath) {
		fonts.emplace_back(std::make_shared<FontGenerator::GameFontdataFixedSizeFont>(
			std::make_shared<FontdataStream>(*GetFileStream(*ppcszFontdataPath)),
			textures));
		ppcszFontdataPath++;
	}

	return { gameFontType, fonts };
}

inline XivRes::FontGenerator::GameFontdataSet XivRes::GameReader::GetFonts(XivRes::GameFontType fontType) const {
	static const char* const fdtListFont[]{
		"common/font/AXIS_96.fdt",
		"common/font/AXIS_12.fdt",
		"common/font/AXIS_14.fdt",
		"common/font/AXIS_18.fdt",
		"common/font/AXIS_36.fdt",
		"common/font/Jupiter_16.fdt",
		"common/font/Jupiter_20.fdt",
		"common/font/Jupiter_23.fdt",
		"common/font/Jupiter_45.fdt",
		"common/font/Jupiter_46.fdt",
		"common/font/Jupiter_90.fdt",
		"common/font/Meidinger_16.fdt",
		"common/font/Meidinger_20.fdt",
		"common/font/Meidinger_40.fdt",
		"common/font/MiedingerMid_10.fdt",
		"common/font/MiedingerMid_12.fdt",
		"common/font/MiedingerMid_14.fdt",
		"common/font/MiedingerMid_18.fdt",
		"common/font/MiedingerMid_36.fdt",
		"common/font/TrumpGothic_184.fdt",
		"common/font/TrumpGothic_23.fdt",
		"common/font/TrumpGothic_34.fdt",
		"common/font/TrumpGothic_68.fdt",
		nullptr,
	};

	static const char* const fdtListFontLobby[]{
		"common/font/AXIS_12_lobby.fdt",
		"common/font/AXIS_14_lobby.fdt",
		"common/font/AXIS_18_lobby.fdt",
		"common/font/AXIS_36_lobby.fdt",
		"common/font/Jupiter_16_lobby.fdt",
		"common/font/Jupiter_20_lobby.fdt",
		"common/font/Jupiter_23_lobby.fdt",
		"common/font/Jupiter_45_lobby.fdt",
		"common/font/Jupiter_46_lobby.fdt",
		"common/font/Jupiter_90_lobby.fdt",
		"common/font/Meidinger_16_lobby.fdt",
		"common/font/Meidinger_20_lobby.fdt",
		"common/font/Meidinger_40_lobby.fdt",
		"common/font/MiedingerMid_10_lobby.fdt",
		"common/font/MiedingerMid_12_lobby.fdt",
		"common/font/MiedingerMid_14_lobby.fdt",
		"common/font/MiedingerMid_18_lobby.fdt",
		"common/font/MiedingerMid_36_lobby.fdt",
		"common/font/TrumpGothic_184_lobby.fdt",
		"common/font/TrumpGothic_23_lobby.fdt",
		"common/font/TrumpGothic_34_lobby.fdt",
		"common/font/TrumpGothic_68_lobby.fdt",
		nullptr,
	};

	static const char* const fdtListKrnAxis[]{
		"common/font/KrnAXIS_120.fdt",
		"common/font/KrnAXIS_140.fdt",
		"common/font/KrnAXIS_180.fdt",
		"common/font/KrnAXIS_360.fdt",
		nullptr,
	};

	static const char* const fdtListChnAxis[]{
		"common/font/ChnAXIS_120.fdt",
		"common/font/ChnAXIS_140.fdt",
		"common/font/ChnAXIS_180.fdt",
		"common/font/ChnAXIS_360.fdt",
		nullptr,
	};

	switch (fontType) {
		case XivRes::GameFontType::font:
			return GetFonts(fontType, fdtListFont, "common/font/font{}.tex");

		case XivRes::GameFontType::font_lobby:
			return GetFonts(fontType, fdtListFontLobby, "common/font/font_lobby{}.tex");

		case XivRes::GameFontType::chn_axis:
			return GetFonts(fontType, fdtListChnAxis, "common/font/font_chn_{}.tex");

		case XivRes::GameFontType::krn_axis:
			return GetFonts(fontType, fdtListKrnAxis, "common/font/font_krn_{}.tex");

		default:
			throw std::invalid_argument("Invalid font specified");
	}
}

#endif
