#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EmptyOrObfuscatedStreamDecoder.h"

Sqex::Sqpack::EmptyStreamDecoder::EmptyStreamDecoder(const Sqex::Sqpack::SqData::FileEntryHeader& header, std::shared_ptr<const Sqex::Sqpack::EntryProvider> stream)
	: StreamDecoder(std::move(stream)) {
	if (header.DecompressedSize < header.BlockCountOrVersion) {
		auto src = m_stream->ReadStreamIntoVector<uint8_t>(header.HeaderSize, header.DecompressedSize);
		m_inflater.emplace(-MAX_WBITS, header.DecompressedSize);
		m_provider.emplace(m_stream->PathSpec(), std::make_shared<Sqex::MemoryRandomAccessStream>((*m_inflater)(src)));

		// Practically unimplemented
		__debugbreak();
	} else {
		m_partialView = std::make_shared<Sqex::RandomAccessStreamPartialView>(m_stream, header.HeaderSize);
		m_provider.emplace(m_stream->PathSpec(), m_partialView);
	}
}

uint64_t Sqex::Sqpack::EmptyStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	return m_provider->ReadStreamPartial(offset, buf, length);
}
