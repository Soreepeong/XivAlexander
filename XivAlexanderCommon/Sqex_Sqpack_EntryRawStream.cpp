#include "pch.h"
#include "Sqex_Sqpack_EntryRawStream.h"

#include "XaZlib.h"

Sqex::Sqpack::EntryRawStream::BinaryStreamDecoder::BinaryStreamDecoder(const EntryRawStream* stream)
	: StreamDecoder(stream)
	, MaxBlockSize(0) {
	const auto locators = Underlying().ReadStreamIntoVector<SqData::BlockHeaderLocator>(
		sizeof SqData::FileEntryHeader,
		EntryHeader().BlockCountOrVersion);

	uint32_t rawFileOffset = 0;
	for (const auto& locator : locators) {
		m_offsets.emplace_back(rawFileOffset);
		m_blockOffsets.emplace_back(EntryHeader().HeaderSize + locator.Offset);
		rawFileOffset += locator.DecompressedDataSize;
		MaxBlockSize = std::max(MaxBlockSize, locator.BlockSize.Value());
	}

	if (rawFileOffset < EntryHeader().DecompressedSize)
		throw CorruptDataException("Data truncated (sum(BlockHeaderLocator.DecompressedDataSize) < FileEntryHeader.DecompresedSize)");
}

static void Decompress(std::span<uint8_t> data, std::span<uint8_t> buffer) {
	z_stream stream{};
	stream.next_in = &data[0];
	stream.avail_in = static_cast<uInt>(data.size_bytes());
	if (const auto res = inflateInit2(&stream, -15); res != Z_OK)
		throw Utils::ZlibError(res);
	const auto inflateCleanup = Utils::CallOnDestruction([&stream]() { inflateEnd(&stream);  });
	
	stream.avail_out = static_cast<uInt>(buffer.size());
	stream.next_out = &buffer[0];
	const auto res = inflate(&stream, Z_FINISH);
	if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
		throw Utils::ZlibError(res);
	if (stream.avail_out)
		throw Sqex::CorruptDataException("Not enough data produced");
	inflateEnd(&stream);
}

uint64_t Sqex::Sqpack::EntryRawStream::BinaryStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));

	auto it = std::lower_bound(m_offsets.begin(), m_offsets.end(), static_cast<uint32_t>(offset));
	if (it != m_offsets.end() && *it > offset)
		--it;

	relativeOffset -= *it;

	std::vector<uint8_t> blockBuffer(MaxBlockSize);
	std::vector<uint8_t> rawBuffer;
	for (const auto blockOffset : std::span(m_blockOffsets).subspan(std::distance(m_offsets.begin(), it))) {
		const auto read = std::span(&blockBuffer[0], static_cast<size_t>(Underlying().ReadStreamPartial(blockOffset, &blockBuffer[0], blockBuffer.size())));
		const auto& blockHeader = *reinterpret_cast<const SqData::BlockHeader*>(&blockBuffer[0]);

		auto decompressionTarget = out.subspan(0, std::min(out.size_bytes(), static_cast<size_t>(blockHeader.DecompressedSize - relativeOffset)));
		if (blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed) {
			std::copy_n(&read[sizeof blockHeader], decompressionTarget.size(), decompressionTarget.begin());
		} else {
			if (relativeOffset) {
				rawBuffer.resize(blockHeader.DecompressedSize);
				decompressionTarget = std::span(rawBuffer);
			}

			rawBuffer.resize(blockHeader.DecompressedSize);
			if (sizeof blockHeader + blockHeader.CompressedSize > read.size_bytes())
				throw CorruptDataException("Failed to read block");

			Decompress(std::span(&read[sizeof blockHeader], blockHeader.CompressedSize), decompressionTarget);

			if (relativeOffset)
				std::copy_n(&decompressionTarget[static_cast<size_t>(relativeOffset)],
					decompressionTarget.size_bytes(),
					&out[0]);
		}
		out = out.subspan(decompressionTarget.size_bytes());

		if (out.empty())
			return length;

		relativeOffset = 0;
	}

	return length - out.size_bytes();
}

Sqex::Sqpack::EntryRawStream::TextureStreamDecoder::TextureStreamDecoder(const EntryRawStream* stream)
	: StreamDecoder(stream) {
}

uint64_t Sqex::Sqpack::EntryRawStream::TextureStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	return 0;
}

Sqex::Sqpack::EntryRawStream::ModelStreamDecoder::ModelStreamDecoder(const EntryRawStream* stream)
	: StreamDecoder(stream) {
}

uint64_t Sqex::Sqpack::EntryRawStream::ModelStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	return 0;
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
	) {
}
