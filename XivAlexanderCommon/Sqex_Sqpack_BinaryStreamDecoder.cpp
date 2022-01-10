#include "pch.h"
#include "Sqex_Sqpack_BinaryStreamDecoder.h"

#include "XaZlib.h"

Sqex::Sqpack::BinaryStreamDecoder::BinaryStreamDecoder(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream)
	: StreamDecoder(std::move(stream)) {
	const auto locators = m_stream->ReadStreamIntoVector<SqData::BlockHeaderLocator>(
		sizeof SqData::FileEntryHeader,
		header.BlockCountOrVersion);

	uint32_t rawFileOffset = 0;
	for (const auto& locator : locators) {
		m_offsets.emplace_back(rawFileOffset);
		m_blockOffsets.emplace_back(header.HeaderSize + locator.Offset);
		m_maxBlockSize = std::max<size_t>(m_maxBlockSize, locator.BlockSize.Value());
		rawFileOffset += locator.DecompressedDataSize;
	}

	if (rawFileOffset < header.DecompressedSize)
		throw CorruptDataException("Data truncated (sum(BlockHeaderLocator.DecompressedDataSize) < FileEntryHeader.DecompresedSize)");
}

uint64_t Sqex::Sqpack::BinaryStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	if (!length)
		return 0;

	auto it = static_cast<size_t>(std::distance(m_offsets.begin(), std::ranges::lower_bound(m_offsets, static_cast<uint32_t>(offset))));
	if (it && (it == m_offsets.size() || (it != m_offsets.size() && m_offsets[it] > offset)))
		--it;

	ReadStreamState info{
		.Underlying = *m_stream,
		.TargetBuffer = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
		.ReadBuffer = std::vector<uint8_t>(m_maxBlockSize),
		.RelativeOffset = offset - m_offsets[it],
		.RequestOffsetVerify = m_offsets[it],
	};

	for (; it < m_offsets.size(); ++it) {
		info.Progress(m_offsets[it], m_blockOffsets[it]);
		if (info.TargetBuffer.empty())
			break;
	}

	m_maxBlockSize = info.ReadBuffer.size();
	return length - info.TargetBuffer.size_bytes();
}