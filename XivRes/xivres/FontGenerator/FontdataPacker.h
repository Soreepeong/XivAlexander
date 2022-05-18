#pragma once

#include "IFixedSizeFont.h"

#include "../Internal/BitmapCopy.h"
#include "../Internal/ThreadPool.h"

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include "../Internal/TeamHypersomnia-rectpack2D/src/finders_interface.h"

#pragma pop_macro("max")
#pragma pop_macro("min")

namespace XivRes::FontGenerator {
	class FontdataPacker {
		size_t m_nThreads = 8;
		int m_nSideLength = 1024;
		std::vector<std::shared_ptr<IFixedSizeFont>> m_fonts;

	public:
		size_t AddFont(std::shared_ptr<IFixedSizeFont> font) {
			m_fonts.emplace_back(std::move(font));
			return m_fonts.size() - 1;
		}

		std::shared_ptr<IFixedSizeFont> GetFont(size_t index) const {
			return m_fonts.at(index);
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

			Internal::ThreadPool pool(m_nThreads);
			for (size_t i = 0; i < m_fonts.size(); i++) {
				const auto& font = m_fonts[i];
				auto& targetFont = *(targetFonts[i] = std::make_shared<FontdataStream>());
				targetFont.TextureWidth(m_nSideLength);
				targetFont.TextureHeight(m_nSideLength);
				targetFont.Points(font->GetSizePt());
				targetFont.LineHeight(font->GetLineHeight());
				targetFont.Ascent(font->GetAscent());
				targetFont.ReserveFontEntries(font->GetCodepointCount());

				pool.Submit([&font, &targetFont]() {
					const auto pairs = font->GetKerningPairs();
					targetFont.ReserveKerningEntries(pairs.size());
					for (const auto& kerning : pairs) {
						if (kerning.second)
							targetFont.AddKerning(kerning.first.first, kerning.first.second, kerning.second);
					}
				});
			}

			for (size_t i = 0; i < m_fonts.size(); i++) {
				const auto& font = m_fonts[i];
				for (const auto& codepoint : font->GetAllCodepoints()) {
					auto& rectangleInfo = rectangleInfos[font->GetGlyphUniqid(codepoint)];
					if (rectangleInfo.TargetFonts.empty()) {
						GlyphMetrics gm;
						if (!font->GetGlyphMetrics(codepoint, gm))
							throw std::runtime_error("Font reported to have a codepoint but it's failing to report glyph metrics");

						rectangleInfo.SourceFont = font.get();
						rectangleInfo.CurrentOffsetX = (std::min)(0, gm.X1);
						rectangleInfo.Entry.Char(codepoint);
						rectangleInfo.Entry.CurrentOffsetY = gm.Y1;
						rectangleInfo.Entry.BoundingWidth = Internal::RangeCheckedCast<uint8_t>(gm.X2 - rectangleInfo.CurrentOffsetX);
						rectangleInfo.Entry.BoundingHeight = Internal::RangeCheckedCast<uint8_t>(gm.Y2 - rectangleInfo.Entry.CurrentOffsetY);
						rectangleInfo.Entry.NextOffsetX = gm.AdvanceX - rectangleInfo.Entry.BoundingWidth;
						pendingRectangles.emplace_back(0, 0, rectangleInfo.Entry.BoundingWidth + 1, rectangleInfo.Entry.BoundingHeight + 1);
						pendingRectangleInfos.emplace_back(&rectangleInfo);
					}

					rectangleInfo.TargetFonts.emplace_back(targetFonts[i].get());
				}
			}

			std::vector<RectangleInfo*> successes, failures;
			failures.reserve(nMaxCharacterCount);

			std::vector<std::shared_ptr<MemoryMipmapStream>> mipmapStreams;

			auto report_successful = [this, &successes, &pendingRectangles, &pendingRectangleInfos, &nPlaneCount](rect_type& r) {
				const auto index = &r - &pendingRectangles.front();
				const auto pInfo = pendingRectangleInfos[index];

				pInfo->Entry.TextureIndex = Internal::RangeCheckedCast<uint16_t>(nPlaneCount);
				pInfo->Entry.TextureOffsetX = Internal::RangeCheckedCast<uint16_t>(r.x + 1);
				pInfo->Entry.TextureOffsetY = Internal::RangeCheckedCast<uint16_t>(r.y + 1);
				
				for (auto& pFontdata : pInfo->TargetFonts)
					pFontdata->AddFontEntry(pInfo->Entry);

				successes.emplace_back(pInfo);

				return callback_result::CONTINUE_PACKING;
			};

			auto report_unsuccessful = [&failures, &pendingRectangles, &pendingRectangleInfos](rect_type& r) {
				failures.emplace_back(pendingRectangleInfos[&r - &pendingRectangles.front()]);
				return callback_result::CONTINUE_PACKING;
			};

			while (!pendingRectangles.empty()) {
				successes.reserve(nMaxCharacterCount);

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

				if (!successes.empty()) {
					for (size_t i = mipmapStreams.size(), i_ = (nPlaneCount + 4) / 4; i < i_; i++)
						mipmapStreams.emplace_back(std::make_shared<XivRes::MemoryMipmapStream>(m_nSideLength, m_nSideLength, 1, XivRes::TextureFormat::A8R8G8B8));
					const auto& pStream = mipmapStreams[nPlaneCount >> 2];
					const auto pCurrentTargetBuffer = &pStream->View<uint8_t>()[3 - (nPlaneCount & 3)];

					auto pSuccesses = std::make_shared<std::vector<RectangleInfo*>>(std::move(successes));
					successes = {};

					const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(pSuccesses->size()))));

					for (size_t nBase = 0; nBase < divideUnit; nBase++) {
						pool.Submit([divideUnit, pSuccesses, nBase, pCurrentTargetBuffer, w = pStream->Width, h = pStream->Height]() {
							for (size_t i = nBase; i < pSuccesses->size(); i += divideUnit) {
								const auto pInfo = (*pSuccesses)[i];
								pInfo->SourceFont->Draw(
									pInfo->Entry.Char(),
									pCurrentTargetBuffer,
									4,
									pInfo->Entry.TextureOffsetX - pInfo->CurrentOffsetX,
									pInfo->Entry.TextureOffsetY - pInfo->Entry.CurrentOffsetY,
									w,
									h,
									255, 0, 255, 255
								);
							}
						});
					}
				}

				pendingRectangles.clear();
				pendingRectangleInfos.clear();
				for (const auto pInfo : failures) {
					pendingRectangles.emplace_back(0, 0, pInfo->Entry.BoundingWidth + 1, pInfo->Entry.BoundingHeight + 1);
					pendingRectangleInfos.emplace_back(pInfo);
				}
				failures.clear();
				nPlaneCount++;
			}

			pool.SubmitDoneAndWait();

			mipmapStreams.resize((nPlaneCount + 3) / 4);
			return std::make_pair(targetFonts, mipmapStreams);
		}
	};
}
