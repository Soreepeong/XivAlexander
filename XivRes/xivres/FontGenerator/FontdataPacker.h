#pragma once

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include "../Internal/TeamHypersomnia-rectpack2D/src/finders_interface.h"

#pragma pop_macro("max")
#pragma pop_macro("min")

namespace XivRes::FontGenerator {
	class FontdataPacker {
		int m_nSideLength = 1024;
		std::vector<std::shared_ptr<IFixedSizeFont>> m_fonts;

	public:
		size_t AddFont(std::shared_ptr<IFixedSizeFont> font) {
			m_fonts.emplace_back(std::move(font));
			return m_fonts.size() - 1;
		}

		auto Compile() {
			using namespace rectpack2D;
			using spaces_type = rectpack2D::empty_spaces<false, default_empty_spaces>;
			using rect_type = output_rect_t<spaces_type>;

			size_t nMaxCharacterCount = 0;
			for (const auto& font : m_fonts)
				nMaxCharacterCount += font->GetCodepointCount();

			struct RectangleInfo {
				IFixedSizeFont* SourceFont{};
				std::vector<FontdataStream*> TargetFonts{};
				char32_t Codepoint{};
				int8_t CurrentOffsetX{};
				FontdataGlyphEntry Entry{};
			};

			std::vector<std::shared_ptr<FontdataStream>> targetFonts;
			targetFonts.resize(m_fonts.size());

			size_t nPlaneCount = 0;
			std::map<const void*, RectangleInfo> rectangleInfos;
			std::vector<rect_type> pendingRectangles;
			std::vector<RectangleInfo*> pendingRectangleInfos;
			pendingRectangles.reserve(nMaxCharacterCount);
			pendingRectangleInfos.reserve(nMaxCharacterCount);

			for (size_t i = 0; i < m_fonts.size(); i++) {
				const auto& font = m_fonts[i];
				auto& targetFont = *(targetFonts[i] = std::make_shared<FontdataStream>());
				targetFont.TextureWidth(m_nSideLength);
				targetFont.TextureHeight(m_nSideLength);
				targetFont.Points(font->GetSizePt());
				targetFont.LineHeight(font->GetLineHeight());
				targetFont.Ascent(font->GetAscent());
				targetFont.ReserveStorage(font->GetCodepointCount(), font->GetKerningEntryCount());

				for (const auto& kerning : font->GetKerningPairs()) {
					if (kerning.second)
						targetFont.AddKerning(kerning.first.first, kerning.first.second, kerning.second);
				}

				for (const auto& codepoint : font->GetAllCodepoints()) {
					auto& rectangleInfo = rectangleInfos[font->GetGlyphUniqid(codepoint)];
					if (rectangleInfo.TargetFonts.empty()) {
						GlyphMetrics gm;
						if (!font->GetGlyphMetrics(codepoint, gm))
							throw std::runtime_error("Font reported to have a codepoint but it's failing to report glyph metrics");

						rectangleInfo.Codepoint = codepoint;
						rectangleInfo.SourceFont = font.get();
						rectangleInfo.CurrentOffsetX = (std::min)(0, gm.X1);
						rectangleInfo.Entry.CurrentOffsetY = gm.Y1;
						rectangleInfo.Entry.BoundingWidth = Internal::RangeCheckedCast<uint8_t>(gm.X2 - rectangleInfo.CurrentOffsetX);
						rectangleInfo.Entry.BoundingHeight = Internal::RangeCheckedCast<uint8_t>(gm.Y2 - rectangleInfo.Entry.CurrentOffsetY);
						rectangleInfo.Entry.NextOffsetX = gm.AdvanceX - rectangleInfo.Entry.BoundingWidth;
						pendingRectangles.emplace_back(0, 0, rectangleInfo.Entry.BoundingWidth + 1, rectangleInfo.Entry.BoundingHeight + 1);
						pendingRectangleInfos.emplace_back(&rectangleInfo);
					}
					rectangleInfo.TargetFonts.emplace_back(&targetFont);
				}
			}

			std::vector<RectangleInfo*> failures;
			failures.reserve(nMaxCharacterCount);

			auto report_successful = [&pendingRectangles, &pendingRectangleInfos, &nPlaneCount](rect_type& r) {
				const auto index = &r - &pendingRectangles.front();
				const auto pInfo = pendingRectangleInfos[index];
				for (auto& pFontdata : pInfo->TargetFonts) {
					pFontdata->AddFontEntry(
						pInfo->Codepoint,
						pInfo->Entry.TextureIndex = Internal::RangeCheckedCast<uint16_t>(nPlaneCount),
						pInfo->Entry.TextureOffsetX = Internal::RangeCheckedCast<uint16_t>(r.x + 1),
						pInfo->Entry.TextureOffsetY = Internal::RangeCheckedCast<uint16_t>(r.y + 1),
						pInfo->Entry.BoundingWidth,
						pInfo->Entry.BoundingHeight,
						pInfo->Entry.NextOffsetX,
						pInfo->Entry.CurrentOffsetY
					);
				}
				return callback_result::CONTINUE_PACKING;
			};

			auto report_unsuccessful = [&failures, &pendingRectangles, &pendingRectangleInfos](rect_type& r) {
				failures.emplace_back(pendingRectangleInfos[&r - &pendingRectangles.front()]);
				return callback_result::CONTINUE_PACKING;
			};

			while (!pendingRectangles.empty()) {
				find_best_packing<spaces_type>(
					pendingRectangles,
					make_finder_input(
						m_nSideLength,
						1,
						report_successful,
						report_unsuccessful,
						flipping_option::DISABLED
					)
				);

				pendingRectangles.clear();
				pendingRectangleInfos.clear();
				for (const auto pInfo : failures) {
					pendingRectangles.emplace_back(0, 0, pInfo->Entry.BoundingWidth + 1, pInfo->Entry.BoundingHeight + 1);
					pendingRectangleInfos.emplace_back(pInfo);
				}
				failures.clear();
				nPlaneCount++;
			}

			std::vector<std::shared_ptr<MemoryMipmapStream>> mipmapStreams;
			mipmapStreams.resize((nPlaneCount + 3) / 4);
			for (size_t i = 0; i < mipmapStreams.size(); i++)
				mipmapStreams[i] = std::make_shared<XivRes::MemoryMipmapStream>(m_nSideLength, m_nSideLength, 1, XivRes::TextureFormat::A8R8G8B8);

			for (const auto& info : rectangleInfos | std::views::values) {
				info.SourceFont->Draw(
					info.Codepoint,
					&mipmapStreams[info.Entry.TextureFileIndex()]->View<uint8_t>()[3 - info.Entry.TexturePlaneIndex()],
					4,
					info.Entry.TextureOffsetX - info.CurrentOffsetX,
					info.Entry.TextureOffsetY - info.Entry.CurrentOffsetY,
					m_nSideLength,
					m_nSideLength,
					255, 0, 255, 255
				);
			}

			return std::make_pair(targetFonts, mipmapStreams);
		}
	};
}
