#ifndef _XIVRES_TEXTUREPACKEDFILESTREAMDECODER_H_
#define _XIVRES_TEXTUREPACKEDFILESTREAMDECODER_H_

#include "SqpackStreamDecoder.h"
#include "Texture.h"

namespace XivRes {
	class TexturePackedFileStreamDecoder : public BasePackedFileStreamDecoder {
		struct BlockInfo {
			uint32_t RequestOffset;
			uint32_t BlockOffset;
			uint32_t RemainingDecompressedSize;
			std::vector<uint16_t> RemainingBlockSizes;

			bool operator<(uint32_t r) const {
				return RequestOffset < r;
			}
		};

		std::vector<uint8_t> m_head;
		std::vector<BlockInfo> m_blocks;

	public:
		TexturePackedFileStreamDecoder(const PackedFileHeader& header, std::shared_ptr<const PackedFileStream> stream)
			: BasePackedFileStreamDecoder(std::move(stream)) {
			uint64_t readOffset = sizeof PackedFileHeader;
			const auto locators = ReadStreamIntoVector<SqpackTexturePackedFileBlockLocator>(*m_stream, readOffset, header.BlockCountOrVersion);
			readOffset += std::span(locators).size_bytes();

			m_head = ReadStreamIntoVector<uint8_t>(*m_stream, header.HeaderSize, locators[0].FirstBlockOffset);

			const auto& texHeader = *reinterpret_cast<const TextureHeader*>(&m_head[0]);
			const auto mipmapOffsets = Internal::span_cast<uint32_t>(m_head, sizeof texHeader, texHeader.MipmapCount);

			const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (mipmapOffsets[1] - mipmapOffsets[0]) / static_cast<uint32_t>(TextureRawDataLength(texHeader, 0));

			for (uint32_t i = 0; i < locators.size(); ++i) {
				const auto& locator = locators[i];
				const auto mipmapIndex = i / repeatCount;
				const auto mipmapPlaneIndex = i % repeatCount;
				const auto mipmapPlaneSize = static_cast<uint32_t>(TextureRawDataLength(texHeader, mipmapIndex));
				uint32_t baseRequestOffset = 0;
				if (mipmapIndex < mipmapOffsets.size())
					baseRequestOffset = mipmapOffsets[mipmapIndex] - mipmapOffsets[0] + mipmapPlaneSize * mipmapPlaneIndex;
				else if (!m_blocks.empty())
					baseRequestOffset = m_blocks.back().RequestOffset + m_blocks.back().RemainingDecompressedSize;
				else
					baseRequestOffset = 0;
				m_blocks.emplace_back(BlockInfo{
					.RequestOffset = baseRequestOffset,
					.BlockOffset = header.HeaderSize + locator.FirstBlockOffset,
					.RemainingDecompressedSize = locator.DecompressedSize,
					.RemainingBlockSizes = ReadStreamIntoVector<uint16_t>(*m_stream, readOffset, locator.SubBlockCount),
					});
				readOffset += std::span(m_blocks.back().RemainingBlockSizes).size_bytes();
				baseRequestOffset += mipmapPlaneSize;
			}
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) override {
			if (!length)
				return 0;

			ReadStreamState info{
				.TargetBuffer = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
				.RelativeOffset = offset,
			};

			if (info.RelativeOffset < static_cast<std::streamoff>(m_head.size())) {
				const auto available = (std::min)(info.TargetBuffer.size_bytes(), static_cast<size_t>(m_head.size() - info.RelativeOffset));
				const auto src = std::span(m_head).subspan(static_cast<size_t>(info.RelativeOffset), available);
				std::copy_n(src.begin(), available, info.TargetBuffer.begin());
				info.TargetBuffer = info.TargetBuffer.subspan(available);
				info.RelativeOffset = 0;
			} else
				info.RelativeOffset -= m_head.size();

			if (info.TargetBuffer.empty() || m_blocks.empty())
				return length - info.TargetBuffer.size_bytes();

			const auto streamSize = m_blocks.back().RequestOffset + m_blocks.back().RemainingDecompressedSize;

			auto from = std::lower_bound(m_blocks.begin(), m_blocks.end(), static_cast<uint32_t>(info.RelativeOffset));
			if (from == m_blocks.end() && info.RelativeOffset >= streamSize)
				return 0;
			if (from != m_blocks.begin() && from->RequestOffset > info.RelativeOffset)
				from -= 1;

			for (auto it = from; it != m_blocks.end(); ) {
				info.ProgressRead(*m_stream, it->BlockOffset, it->RemainingBlockSizes.empty() ? 16384 : it->RemainingBlockSizes.front());
				info.ProgressDecode(it->RequestOffset);

				if (it->RemainingBlockSizes.empty()) {
					++it;
				} else {
					const auto& blockHeader = info.AsHeader();
					auto newBlockInfo = BlockInfo{
						.RequestOffset = it->RequestOffset + blockHeader.DecompressedSize,
						.BlockOffset = it->BlockOffset + it->RemainingBlockSizes.front(),
						.RemainingDecompressedSize = it->RemainingDecompressedSize - blockHeader.DecompressedSize,
						.RemainingBlockSizes = std::move(it->RemainingBlockSizes),
					};

					++it;
					if (it == m_blocks.end() || (it->RequestOffset != newBlockInfo.RequestOffset && it->BlockOffset != newBlockInfo.BlockOffset)) {
						newBlockInfo.RemainingBlockSizes.erase(newBlockInfo.RemainingBlockSizes.begin());
						it = m_blocks.emplace(it, std::move(newBlockInfo));
					}
				}

				if (info.TargetBuffer.empty())
					break;
			}

			return length - info.TargetBuffer.size_bytes();
		}
	};
}

#endif
