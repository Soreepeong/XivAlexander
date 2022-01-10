#include "pch.h"
#include "Sqex_Sqpack_EntryRawStream.h"

#include "Sqex_Sqpack_StreamDecoder.h"
#include "Sqex_Texture.h"
#include "XaZlib.h"

Sqex::Sqpack::EntryRawStream::EntryRawStream(std::shared_ptr<const EntryProvider> provider)
	: m_provider(std::move(provider))
	, m_entryHeader(m_provider->ReadStream<SqData::FileEntryHeader>(0))
	, m_decoder(StreamDecoder::CreateNew(m_entryHeader, m_provider)) {
}

Sqex::Sqpack::EntryRawStream::~EntryRawStream() = default;

uint64_t Sqex::Sqpack::EntryRawStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (!m_decoder)
		return 0;
	const auto fullSize = m_entryHeader.DecompressedSize.Value();
	if (offset >= fullSize)
		return 0;
	if (offset + length > fullSize)
		length = fullSize - offset;

	const auto decompressedSize = m_entryHeader.DecompressedSize.Value();
	auto read = m_decoder->ReadStreamPartial(offset, buf, length);
	if (read != length)
		std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
	return length;
}

uint64_t Sqex::Sqpack::EntryRawStream::StreamSize() const {
	return m_decoder ? m_entryHeader.DecompressedSize.Value() : 0;
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::EntryRawStream::EntryType() const {
	return m_provider->EntryType();
}

const Sqex::Sqpack::EntryPathSpec& Sqex::Sqpack::EntryRawStream::PathSpec() const {
	return m_provider->PathSpec();
}
