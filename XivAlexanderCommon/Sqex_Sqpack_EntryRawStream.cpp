#include "pch.h"
#include "Sqex_Sqpack_EntryRawStream.h"

uint64_t Sqex::Sqpack::EntryRawStream::BinaryStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
}

uint64_t Sqex::Sqpack::EntryRawStream::TextureStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
}

uint64_t Sqex::Sqpack::EntryRawStream::ModelStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
}

Sqex::Sqpack::EntryRawStream::EntryRawStream(std::shared_ptr<EntryProvider> provider)
	: m_provider(std::move(provider))
	, m_entryHeader(m_provider->ReadStream<SqData::FileEntryHeader>(0))
	, m_decoder(
		m_entryHeader.Type == SqData::FileEntryType::Empty || m_entryHeader.DecompressedSize == 0 ? nullptr :
		m_entryHeader.Type == SqData::FileEntryType::Binary ? static_cast<std::unique_ptr<StreamDecoder>>(std::make_unique<BinaryStreamDecoder>(this)) :
		m_entryHeader.Type == SqData::FileEntryType::Texture ? static_cast<std::unique_ptr<StreamDecoder>>(std::make_unique<TextureStreamDecoder>(this)) :
		m_entryHeader.Type == SqData::FileEntryType::Model ? static_cast<std::unique_ptr<StreamDecoder>>(std::make_unique<ModelStreamDecoder>(this)) :
		nullptr
	){
}
