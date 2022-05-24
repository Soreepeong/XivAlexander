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
		size_t m_nThreads = std::thread::hardware_concurrency();
		int m_nSideLength = 4096;
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

			struct CharacterPlan {
				char32_t Codepoint;
				const IFixedSizeFont* BaseFont{};
				FontdataGlyphEntry BaseEntry{};
				std::map<FontdataStream*, std::pair<FontdataGlyphEntry, size_t>> TargetFonts{};
				const Unicode::UnicodeBlocks::BlockDefinition* UnicodeBlock{};
				int BaseWidth{};
				int CurrentOffsetX{};
			};

			std::vector<std::shared_ptr<FontdataStream>> targetFonts;
			targetFonts.resize(m_fonts.size());

			std::map<const IFixedSizeFont*, std::vector<std::shared_ptr<IFixedSizeFont>>> threadSafeBaseFonts;
			std::vector<std::vector<std::shared_ptr<IFixedSizeFont>>> threadSafeSourceFonts;

			threadSafeSourceFonts.reserve(m_fonts.size());
			size_t nMaxCharacterCount = 0;
			for (const auto& font : m_fonts) {
				nMaxCharacterCount += font->GetAllCodepoints().size();
				threadSafeSourceFonts.emplace_back();
				threadSafeSourceFonts.back().resize(m_nThreads);
				threadSafeSourceFonts.back()[0] = font;
			}

			const auto GetThreadSafeBaseFont = [&threadSafeBaseFonts](const IFixedSizeFont* font, size_t threadIndex) -> const IFixedSizeFont& {
				auto& copy = threadSafeBaseFonts[font][threadIndex];
				if (!copy)
					copy = threadSafeBaseFonts[font][0]->GetThreadSafeView();
				return *copy;
			};

			const auto GetThreadSafeSourceFont = [&threadSafeSourceFonts](size_t fontIndex, size_t threadIndex) -> const IFixedSizeFont& {
				auto& copy = threadSafeSourceFonts[fontIndex][threadIndex];
				if (!copy)
					copy = threadSafeSourceFonts[fontIndex][0]->GetThreadSafeView();
				return *copy;
			};

			std::vector<CharacterPlan> rectangleInfoList;
			rectangleInfoList.reserve(nMaxCharacterCount);
			{
				for (size_t i = 0; i < m_fonts.size(); i++) {
					auto& targetFont = *(targetFonts[i] = std::make_shared<FontdataStream>());

					const auto& font = *m_fonts[i];
					targetFont.TextureWidth(m_nSideLength);
					targetFont.TextureHeight(m_nSideLength);
					targetFont.Size(font.GetSize());
					targetFont.LineHeight(font.GetLineHeight());
					targetFont.Ascent(font.GetAscent());
					targetFont.ReserveFontEntries(font.GetAllCodepoints().size());
					targetFont.ReserveKerningEntries(font.GetAllKerningPairs().size());
					for (const auto& kerning : font.GetAllKerningPairs()) {
						if (kerning.second)
							targetFont.AddKerning(kerning.first.first, kerning.first.second, kerning.second);
					}
				}

				std::map<const void*, CharacterPlan*> rectangleInfoMap;
				for (size_t i = 0; i < m_fonts.size(); i++) {
					const auto& font = m_fonts[i];
					for (const auto& codepoint : font->GetAllCodepoints()) {
						auto& block = Unicode::UnicodeBlocks::GetCorrespondingBlock(codepoint);
						if (block.Flags & Unicode::UnicodeBlocks::RTL)
							continue;

						auto& pInfo = rectangleInfoMap[font->GetGlyphUniqid(codepoint)];
						if (!pInfo) {
							rectangleInfoList.emplace_back();
							pInfo = &rectangleInfoList.back();
							pInfo->Codepoint = codepoint;
							pInfo->BaseFont = font->GetBaseFont(codepoint);
							if (threadSafeBaseFonts[pInfo->BaseFont].empty()) {
								threadSafeBaseFonts[pInfo->BaseFont].resize(m_nThreads);
								threadSafeBaseFonts[pInfo->BaseFont][0] = pInfo->BaseFont->GetThreadSafeView();
							}
							pInfo->UnicodeBlock = &block;
							pInfo->BaseEntry.Char(codepoint);
						}
						pInfo->TargetFonts[targetFonts[i].get()] = std::make_pair(FontdataGlyphEntry{
							.Utf8Value = pInfo->BaseEntry.Utf8Value,
							.ShiftJisValue = pInfo->BaseEntry.ShiftJisValue,
						}, i);
						targetFonts[i]->AddFontEntry(codepoint, 0, 0, 0, 0, 0, 0, 0);
					}
				}
			}

			{
				Internal::ThreadPool pool(m_nThreads);

				size_t remaining = rectangleInfoList.size();
				const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(rectangleInfoList.size()))));
				for (size_t nBase = 0; nBase < divideUnit; nBase++) {
					pool.Submit([&remaining, divideUnit, &rectangleInfoList, &pool, nBase, &GetThreadSafeSourceFont, &GetThreadSafeBaseFont](size_t nThreadIndex) {
						for (size_t i = nBase; i < rectangleInfoList.size(); i += divideUnit) {
							remaining -= 1;
							pool.AbortIfErrorOccurred();

							auto& info = rectangleInfoList[i];
							const auto& baseFont = GetThreadSafeBaseFont(info.BaseFont, nThreadIndex);

							GlyphMetrics gm;
							if (!baseFont.GetGlyphMetrics(info.Codepoint, gm))
								throw std::runtime_error("Base font reported to have a codepoint but it's failing to report glyph metrics");
							info.CurrentOffsetX = (std::min)(0, gm.X1);
							info.BaseEntry.CurrentOffsetY = gm.Y1;
							info.BaseEntry.BoundingHeight = Internal::RangeCheckedCast<uint8_t>(gm.Y2 - gm.Y1);
							info.BaseEntry.BoundingWidth = Internal::RangeCheckedCast<uint8_t>(gm.X2 - info.CurrentOffsetX);
							info.BaseEntry.NextOffsetX = gm.AdvanceX - gm.X2;
							info.BaseWidth = *info.BaseEntry.BoundingWidth;

							for (auto& [fdt, entryAndSourceFontIndex] : info.TargetFonts) {
								auto& [entry, sourceFontIndex] = entryAndSourceFontIndex;
								const auto& sourceFont = GetThreadSafeSourceFont(sourceFontIndex, nThreadIndex);
								if (!sourceFont.GetGlyphMetrics(info.Codepoint, gm)) {
									sourceFont.GetGlyphMetrics(info.Codepoint, gm);
									throw std::runtime_error("Font reported to have a codepoint but it's failing to report glyph metrics");
								}

								entry.CurrentOffsetY = gm.Y1;
								entry.BoundingHeight = Internal::RangeCheckedCast<uint8_t>(gm.Y2 - gm.Y1);
								entry.BoundingWidth = Internal::RangeCheckedCast<uint8_t>(gm.X2 - (std::min)(0, gm.X1));
								entry.NextOffsetX = gm.AdvanceX - gm.X2;

								if (*info.BaseEntry.BoundingWidth < *entry.BoundingWidth) {
									info.CurrentOffsetX -= *entry.BoundingWidth - *info.BaseEntry.BoundingWidth;
									info.BaseEntry.BoundingWidth = *entry.BoundingWidth;
								}
							}
						}
					});
				}

				pool.SubmitDoneAndWait();
			}

			size_t nPlaneCount = 0;
			std::vector<rect_type> pendingRectangles;
			std::vector<CharacterPlan*> pendingPlans;
			std::vector<CharacterPlan*> successfulPlans, failedPlans;
			pendingRectangles.reserve(nMaxCharacterCount);
			pendingPlans.reserve(nMaxCharacterCount);
			failedPlans.reserve(nMaxCharacterCount);

			auto report_successful = [this, &successfulPlans, &pendingRectangles, &pendingPlans, &nPlaneCount](rect_type& r) {
				const auto index = &r - &pendingRectangles.front();
				const auto pInfo = pendingPlans[index];

				pInfo->BaseEntry.TextureIndex = Internal::RangeCheckedCast<uint16_t>(nPlaneCount);
				pInfo->BaseEntry.TextureOffsetX = Internal::RangeCheckedCast<uint16_t>(r.x + 1);
				pInfo->BaseEntry.TextureOffsetY = Internal::RangeCheckedCast<uint16_t>(r.y + 1);

				for (auto& [pFontdata, entryAndMore] : pInfo->TargetFonts) {
					auto& [entry, _] = entryAndMore;
					entry.TextureIndex = pInfo->BaseEntry.TextureIndex;
					entry.TextureOffsetX = pInfo->BaseEntry.TextureOffsetX;
					entry.TextureOffsetY = pInfo->BaseEntry.TextureOffsetY;

					if (const auto extra = *entry.BoundingWidth - pInfo->BaseWidth; extra > 0) {
						entry.BoundingWidth = *entry.BoundingWidth + extra;
						entry.TextureOffsetX = *entry.TextureOffsetX - extra;
					}
					if (const auto diff = *entry.CurrentOffsetY - pInfo->BaseEntry.CurrentOffsetY)
						entry.TextureOffsetY = *entry.TextureOffsetY + diff;

					pFontdata->AddFontEntry(entry);
				}

				successfulPlans.emplace_back(pInfo);

				return callback_result::CONTINUE_PACKING;
			};

			auto report_unsuccessful = [&failedPlans, &pendingRectangles, &pendingPlans](rect_type& r) {
				failedPlans.emplace_back(pendingPlans[&r - &pendingRectangles.front()]);
				return callback_result::CONTINUE_PACKING;
			};

			std::vector<std::shared_ptr<MemoryMipmapStream>> mipmapStreams;
			{
				Internal::ThreadPool pool(m_nThreads);

				for (auto& rectangleInfo : rectangleInfoList) {
					pendingRectangles.emplace_back(0, 0, rectangleInfo.BaseEntry.BoundingWidth + 1, rectangleInfo.BaseEntry.BoundingHeight + 1);
					pendingPlans.emplace_back(&rectangleInfo);
				}

				while (!pendingRectangles.empty()) {
					successfulPlans.reserve(nMaxCharacterCount);

					if (nPlaneCount == 0) {
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

					} else {
						// Already sorted from above
						find_best_packing_dont_sort<spaces_type>(
							pendingRectangles,
							make_finder_input(
								m_nSideLength,
								1,
								report_successful,
								report_unsuccessful,
								flipping_option::DISABLED
							)
						);
					}

					if (!successfulPlans.empty()) {
						for (size_t i = mipmapStreams.size(), i_ = (nPlaneCount + 4) / 4; i < i_; i++)
							mipmapStreams.emplace_back(std::make_shared<XivRes::MemoryMipmapStream>(m_nSideLength, m_nSideLength, 1, XivRes::TextureFormat::A8R8G8B8));
						const auto& pStream = mipmapStreams[nPlaneCount >> 2];
						const auto pCurrentTargetBuffer = &pStream->View<uint8_t>()[3 - (nPlaneCount & 3)];

						auto pSuccesses = std::make_shared<std::vector<CharacterPlan*>>(std::move(successfulPlans));
						successfulPlans = {};

						const auto divideUnit = (std::max<size_t>)(1, static_cast<size_t>(std::sqrt(static_cast<double>(pSuccesses->size()))));

						for (size_t nBase = 0; nBase < divideUnit; nBase++) {
							pool.Submit([divideUnit, pSuccesses, nBase, pCurrentTargetBuffer, w = pStream->Width, h = pStream->Height, &GetThreadSafeBaseFont](size_t nThreadIndex) {
								for (size_t i = nBase; i < pSuccesses->size(); i += divideUnit) {
									const auto pInfo = (*pSuccesses)[i];
									const auto& font = GetThreadSafeBaseFont(pInfo->BaseFont, nThreadIndex);
									font.Draw(
										pInfo->BaseEntry.Char(),
										pCurrentTargetBuffer,
										4,
										pInfo->BaseEntry.TextureOffsetX - pInfo->CurrentOffsetX,
										pInfo->BaseEntry.TextureOffsetY - pInfo->BaseEntry.CurrentOffsetY,
										w,
										h,
										255, 0, 255, 255, 1.0f
									);
								}
							});
						}
					}

					pendingRectangles.clear();
					pendingPlans.clear();
					for (const auto pInfo : failedPlans) {
						pendingRectangles.emplace_back(0, 0, pInfo->BaseEntry.BoundingWidth + 1, pInfo->BaseEntry.BoundingHeight + 1);
						pendingPlans.emplace_back(pInfo);
					}
					failedPlans.clear();
					nPlaneCount++;
				}

				pool.SubmitDoneAndWait();
			}

			return std::make_pair(targetFonts, mipmapStreams);
		}
	};
}
