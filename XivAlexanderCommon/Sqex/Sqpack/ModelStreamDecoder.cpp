#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/ModelStreamDecoder.h"

#include "XivAlexanderCommon/Sqex/Model.h"

Sqex::Sqpack::ModelStreamDecoder::ModelStreamDecoder(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream)
	: StreamDecoder(std::move(stream)) {

	const auto AsHeader = [this]() -> Model::Header& { return *reinterpret_cast<Model::Header*>(&m_head[0]); };

	const auto underlyingSize = m_stream->StreamSize();
	uint64_t readOffset = sizeof SqData::FileEntryHeader;
	const auto locator = m_stream->ReadStream<SqData::ModelBlockLocator>(readOffset);
	const auto blockCount = static_cast<size_t>(locator.FirstBlockIndices.Index[2]) + locator.BlockCount.Index[2];

	readOffset += sizeof locator;
	for (const auto blockSize : m_stream->ReadStreamIntoVector<uint16_t>(readOffset, blockCount)) {
		m_blocks.emplace_back(BlockInfo{
			.RequestOffset = 0,
			.BlockOffset = m_blocks.empty() ? header.HeaderSize.Value() : m_blocks.back().BlockOffset + m_blocks.back().PaddedChunkSize,
			.PaddedChunkSize = blockSize,
			.GroupIndex = UINT16_MAX,
			.GroupBlockIndex = 0,
			});
	}

	m_head.resize(sizeof Model::Header);
	AsHeader() = {
		.Version = header.BlockCountOrVersion,
		.VertexDeclarationCount = locator.VertexDeclarationCount,
		.MaterialCount = locator.MaterialCount,
		.LodCount = locator.LodCount,
		.EnableIndexBufferStreaming = locator.EnableIndexBufferStreaming,
		.EnableEdgeGeometry = locator.EnableEdgeGeometry,
		.Padding = locator.Padding,
	};

	if (m_blocks.empty())
		return;

	for (uint16_t i = 0; i < 11; ++i) {
		if (!locator.BlockCount.EntryAt(i))
			continue;

		const size_t blockIndex = locator.FirstBlockIndices.EntryAt(i).Value();
		auto& firstBlock = m_blocks[blockIndex];
		firstBlock.GroupIndex = i;
		firstBlock.GroupBlockIndex = 0;

		for (uint16_t j = 1; j < locator.BlockCount.EntryAt(i); ++j) {
			if (blockIndex + j >= blockCount)
				throw CorruptDataException("Out of bounds index information detected");

			auto& block = m_blocks[blockIndex + j];
			if (block.GroupIndex != UINT16_MAX)
				throw CorruptDataException("Overlapping index information detected");
			block.GroupIndex = i;
			block.GroupBlockIndex = j;
		}
	}

	auto lastOffset = 0;
	for (auto& block : m_blocks) {
		SqData::BlockHeader blockHeader;

		if (block.BlockOffset == underlyingSize)
			blockHeader.DecompressedSize = blockHeader.CompressedSize = 0;
		else
			m_stream->ReadStream(block.BlockOffset, &blockHeader, sizeof blockHeader);

		if (m_maxBlockSize < sizeof blockHeader + blockHeader.CompressedSize)
			m_maxBlockSize = static_cast<uint16_t>(sizeof blockHeader + blockHeader.CompressedSize);

		block.DecompressedSize = static_cast<uint16_t>(blockHeader.DecompressedSize);
		block.RequestOffset = lastOffset;
		lastOffset += block.DecompressedSize;
	}

	for (size_t blkI = locator.FirstBlockIndices.Stack.Value(), i_ = blkI + locator.BlockCount.Stack; blkI < i_; ++blkI)
		AsHeader().StackSize += m_blocks[blkI].DecompressedSize;
	for (size_t blkI = locator.FirstBlockIndices.Runtime.Value(), i_ = blkI + locator.BlockCount.Runtime; blkI < i_; ++blkI)
		AsHeader().RuntimeSize += m_blocks[blkI].DecompressedSize;
	for (size_t lodI = 0; lodI < 3; ++lodI) {
		for (size_t blkI = locator.FirstBlockIndices.Vertex[lodI].Value(), i_ = blkI + locator.BlockCount.Vertex[lodI]; blkI < i_; ++blkI)
			AsHeader().VertexSize[lodI] += m_blocks[blkI].DecompressedSize;
		for (size_t blkI = locator.FirstBlockIndices.Index[lodI].Value(), i_ = blkI + locator.BlockCount.Index[lodI]; blkI < i_; ++blkI)
			AsHeader().IndexSize[lodI] += m_blocks[blkI].DecompressedSize;
		AsHeader().VertexOffset[lodI] = static_cast<uint32_t>(m_head.size() + (locator.FirstBlockIndices.Vertex[lodI] == m_blocks.size() ? lastOffset : m_blocks[locator.FirstBlockIndices.Vertex[lodI]].RequestOffset));
		AsHeader().IndexOffset[lodI] = static_cast<uint32_t>(m_head.size() + (locator.FirstBlockIndices.Index[lodI] == m_blocks.size() ? lastOffset : m_blocks[locator.FirstBlockIndices.Index[lodI]].RequestOffset));
	}
}

uint64_t Sqex::Sqpack::ModelStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
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
		const auto src = std::span(m_head).subspan(static_cast<size_t>(info.RelativeOffset), static_cast<size_t>(available));
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

	info.RequestOffsetVerify = it->RequestOffset;
	info.RelativeOffset -= info.RequestOffsetVerify;

	for (; it != m_blocks.end(); ++it) {
		info.Progress(it->RequestOffset, it->BlockOffset);
		if (info.TargetBuffer.empty())
			break;
	}

	m_maxBlockSize = info.ReadBuffer.size();
	return length - info.TargetBuffer.size_bytes();
}
