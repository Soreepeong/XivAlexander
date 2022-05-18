#ifndef _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_
#define _XIVRES_FONTGENERATOR_GAMEFONTDATAFIXEDSIZEFONT_H_

#include <vector>

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"

namespace XivRes::FontGenerator {
	class GameFontdataFixedSizeFont : public DefaultAbstractFixedSizeFont {
		std::array<uint8_t, 256> m_gammaTable;
		std::shared_ptr<const FontdataStream> m_stream;
		std::vector<std::shared_ptr<MemoryMipmapStream>> m_mipmapStreams;
		int m_dx = 0;

	public:
		GameFontdataFixedSizeFont(std::shared_ptr<const FontdataStream> stream, std::vector<std::shared_ptr<MemoryMipmapStream>> mipmapStreams)
			: m_gammaTable(LinearGammaTable)
			, m_stream(std::move(stream))
			, m_mipmapStreams(std::move(mipmapStreams)) {
			for (const auto& mipmapStream : m_mipmapStreams) {
				if (mipmapStream->Type != TextureFormat::A8R8G8B8)
					throw std::invalid_argument("All mipmap streams must be in A8R8G8B8 format.");
			}

			m_dx = -1;
			for (const auto c : U"-0") {
				const auto pEntry = m_stream->GetFontEntry(c);
				if (!pEntry)
					continue;

				const auto& mipmapStream = *m_mipmapStreams.at(pEntry->TextureFileIndex());
				const auto pOpacityArray = &mipmapStream.View<uint8_t>()[3 - pEntry->TexturePlaneIndex()];

				if (m_dx == -1)
					m_dx = *pEntry->BoundingWidth;
				else
					m_dx = (std::min<int>)(m_dx, *pEntry->BoundingWidth);
				for (size_t x = *pEntry->TextureOffsetX, x_ = x + m_dx; x < x_; x++) {
					auto pass = true;
					for (size_t y = *pEntry->TextureOffsetY, y_ = *pEntry->TextureOffsetY + *pEntry->BoundingWidth; pass && y < y_; y++)
						pass = pOpacityArray[4 * (y * mipmapStream.Width + x)] == 0;
					if (!pass) {
						m_dx = static_cast<int>(x - pEntry->TextureOffsetX) - 1;
						break;
					}
				}
			}

			if (m_dx == -1)
				m_dx = 0;
		}

		int GetHorizontalOffset() const {
			return m_dx;
		}

		void SetHorizontalOffset(int offset) {
			m_dx = offset;
		}

		float GetSizePt() const override {
			return m_stream->Points();
		}

		int GetAscent() const override {
			return m_stream->Ascent();
		}

		int GetLineHeight() const override {
			return m_stream->LineHeight();
		}

		size_t GetCodepointCount() const override {
			return m_stream->GetFontTableEntries().size();
		}

		std::set<char32_t> GetAllCodepoints() const override {
			std::vector<char32_t> all;
			all.reserve(m_stream->GetFontTableEntries().size());
			for (const auto& entry : m_stream->GetFontTableEntries())
				all.emplace_back(entry.Char());
			return { all.begin(), all.end() };
		}

		bool GetGlyphMetrics(char32_t codepoint, GlyphMetrics& gm) const override {
			const auto pEntry = m_stream->GetFontEntry(codepoint);
			if (!pEntry) {
				gm.Clear();
				return false;
			}

			gm = GlyphMetricsFromEntry(pEntry);
			return true;
		}

		const void* GetGlyphUniqid(char32_t c) const override {
			return m_stream->GetFontEntry(c);
		}

		size_t GetKerningEntryCount() const override {
			return m_stream->GetKerningEntries().size();
		}

		std::map<std::pair<char32_t, char32_t>, int> GetKerningPairs() const override {
			std::map<std::pair<char32_t, char32_t>, int> res;
			for (const auto& entry : m_stream->GetKerningEntries())
				res.emplace_hint(res.end(), std::make_pair(entry.Left(), entry.Right()), entry.RightOffset);
			return res;
		}

		int GetAdjustedAdvanceX(char32_t left, char32_t right) const override {
			GlyphMetrics gm;
			if (!GetGlyphMetrics(left, gm))
				return 0;

			return gm.AdvanceX + m_stream->GetKerningDistance(left, right);
		}

		const std::array<uint8_t, 256>& GetGammaTable() const override {
			return m_gammaTable;
		}

		void SetGammaTable(const std::array<uint8_t, 256>& gammaTable) override {
			m_gammaTable = gammaTable;
		}

		bool Draw(char32_t codepoint, RGBA8888* pBuf, int drawX, int drawY, int destWidth, int destHeight, RGBA8888 fgColor, RGBA8888 bgColor) const override {
			const auto pEntry = m_stream->GetFontEntry(codepoint);
			if (!pEntry)
				return false;

			auto src = GlyphMetrics{ false, *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_mipmapStreams.at(pEntry->TextureFileIndex());
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			Internal::BitmapCopy()
				.From(&mipmapStream.View<uint8_t>()[3 - pEntry->TexturePlaneIndex()], mipmapStream.Width, mipmapStream.Height, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithBackgroundColor(bgColor)
				.WithGammaTable(m_gammaTable)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

		bool Draw(char32_t codepoint, uint8_t* pBuf, size_t stride, int drawX, int drawY, int destWidth, int destHeight, uint8_t fgColor, uint8_t bgColor, uint8_t fgOpacity, uint8_t bgOpacity) const override {
			const auto pEntry = m_stream->GetFontEntry(codepoint);
			if (!pEntry)
				return false;

			auto src = GlyphMetrics{ false, *pEntry->TextureOffsetX, *pEntry->TextureOffsetY, *pEntry->TextureOffsetX + *pEntry->BoundingWidth, *pEntry->TextureOffsetY + *pEntry->BoundingHeight };
			auto dest = GlyphMetricsFromEntry(pEntry, drawX, drawY);
			const auto& mipmapStream = *m_mipmapStreams.at(pEntry->TextureFileIndex());
			src.AdjustToIntersection(dest, mipmapStream.Width, mipmapStream.Height, destWidth, destHeight);
			Internal::L8BitmapCopy()
				.From(&mipmapStream.View<uint8_t>()[3 - pEntry->TexturePlaneIndex()], mipmapStream.Width, mipmapStream.Height, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.To(pBuf, destWidth, destHeight, 4, Internal::BitmapVerticalDirection::TopRowFirst)
				.WithForegroundColor(fgColor)
				.WithForegroundOpacity(fgOpacity)
				.WithBackgroundColor(bgColor)
				.WithBackgroundOpacity(bgOpacity)
				.WithGammaTable(m_gammaTable)
				.CopyTo(src.X1, src.Y1, src.X2, src.Y2, dest.X1, dest.Y1);
			return true;
		}

	private:
		GlyphMetrics GlyphMetricsFromEntry(const FontdataGlyphEntry* pEntry, int x = 0, int y = 0) const {
			GlyphMetrics src{
				.Empty = false,
				.X1 = x - m_dx,
				.Y1 = y + pEntry->CurrentOffsetY,
				.X2 = src.X1 + pEntry->BoundingWidth,
				.Y2 = src.Y1 + pEntry->BoundingHeight,
				.AdvanceX = pEntry->BoundingWidth + pEntry->NextOffsetX,
			};
			return src;
		}
	};
}

#endif
