#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/TextureStreamDecoder.h"

#include "XivAlexanderCommon/Sqex/Texture.h"

Sqex::Sqpack::TextureStreamDecoder::TextureStreamDecoder(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream)
	: StreamDecoder(std::move(stream)) {
	uint64_t readOffset = sizeof SqData::FileEntryHeader;
	const auto locators = m_stream->ReadStreamIntoVector<SqData::TextureBlockHeaderLocator>(readOffset, header.BlockCountOrVersion);
	readOffset += std::span(locators).size_bytes();

	m_head = m_stream->ReadStreamIntoVector<uint8_t>(header.HeaderSize, locators[0].FirstBlockOffset);

	const auto& texHeader = *reinterpret_cast<const Texture::Header*>(&m_head[0]);
	const auto mipmapOffsets = span_cast<uint32_t>(m_head, sizeof texHeader, texHeader.MipmapCount);

	const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (mipmapOffsets[1] - mipmapOffsets[0]) / static_cast<uint32_t>(Texture::RawDataLength(texHeader, 0));

	for (uint32_t i = 0; i < locators.size(); ++i) {
		const auto& locator = locators[i];
		const auto mipmapIndex = i / repeatCount;
		const auto mipmapPlaneIndex = i % repeatCount;
		const auto mipmapPlaneSize = static_cast<uint32_t>(Texture::RawDataLength(texHeader, mipmapIndex));
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
			.RemainingBlockSizes = m_stream->ReadStreamIntoVector<uint16_t>(readOffset, locator.SubBlockCount),
			});
		readOffset += std::span(m_blocks.back().RemainingBlockSizes).size_bytes();
		baseRequestOffset += mipmapPlaneSize;
	}
}

uint64_t Sqex::Sqpack::TextureStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	if (!length)
		return 0;

	ReadStreamState info{
		.Underlying = *m_stream,
		.TargetBuffer = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
		.ReadBuffer = std::vector<uint8_t>(m_maxBlockSize),
		.RelativeOffset = offset,
	};

	if (info.RelativeOffset < m_head.size()) {
		const auto available = std::min(info.TargetBuffer.size_bytes(), static_cast<size_t>(m_head.size() - info.RelativeOffset));
		const auto src = std::span(m_head).subspan(static_cast<size_t>(info.RelativeOffset), available);
		std::copy_n(src.begin(), available, info.TargetBuffer.begin());
		info.TargetBuffer = info.TargetBuffer.subspan(available);
		info.RelativeOffset = 0;
	} else
		info.RelativeOffset -= m_head.size();

	if (info.TargetBuffer.empty() || m_blocks.empty())
		return length - info.TargetBuffer.size_bytes();

	auto it = std::lower_bound(m_blocks.begin(), m_blocks.end(), static_cast<uint32_t>(info.RelativeOffset), [&](const BlockInfo& l, uint32_t r) {
		return l.RequestOffset < r;
		});
	if (it == m_blocks.end() || (it != m_blocks.end() && it != m_blocks.begin() && it->RequestOffset > info.RelativeOffset))
		--it;

	while (it != m_blocks.end()) {
		info.Progress(it->RequestOffset, it->BlockOffset);

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

	m_maxBlockSize = info.ReadBuffer.size();
	return length - info.TargetBuffer.size_bytes();
}
