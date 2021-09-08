#include "pch.h"
#include "Sqex_Sqpack_EntryRawStream.h"

#include "Sqex_Texture.h"
#include "XaZlib.h"

Sqex::Sqpack::EntryRawStream::BinaryStreamDecoder::BinaryStreamDecoder(const EntryRawStream* stream)
	: StreamDecoder(stream) {
	const auto locators = Underlying().ReadStreamIntoVector<SqData::BlockHeaderLocator>(
		sizeof SqData::FileEntryHeader,
		EntryHeader().BlockCountOrVersion);

	uint32_t rawFileOffset = 0;
	for (const auto& locator : locators) {
		m_offsets.emplace_back(rawFileOffset);
		m_blockOffsets.emplace_back(EntryHeader().HeaderSize + locator.Offset);
		rawFileOffset += locator.DecompressedDataSize;
		MaxBlockSize = std::max<size_t>(MaxBlockSize, locator.BlockSize.Value());
	}

	if (rawFileOffset < EntryHeader().DecompressedSize)
		throw CorruptDataException("Data truncated (sum(BlockHeaderLocator.DecompressedDataSize) < FileEntryHeader.DecompresedSize)");
}

struct Sqex::Sqpack::EntryRawStream::StreamDecoder::ReadStreamState {
	const RandomAccessStream& Underlying;
	std::span<uint8_t> destination;
	std::vector<uint8_t> readBuffer;
	std::vector<uint8_t> zlibBuffer;
	uint64_t relativeOffset = 0;
	uint32_t requestOffsetVerify = 0;
	bool zstreamInitialized = false;
	z_stream zstream{};

	[[nodiscard]] const auto& AsHeader() const {
		return *reinterpret_cast<const SqData::BlockHeader*>(&readBuffer[0]);
	}

	void InitializeZlib() {
		int res;
		if (!zstreamInitialized)
			res = inflateInit2(&zstream, -15);
		else
			res = inflateReset2(&zstream, -15);
		if (res != Z_OK)
			throw Utils::ZlibError(res);
	}

	~ReadStreamState() {
		if (zstreamInitialized)
			inflateEnd(&zstream);
	}

private:
	void AttemptSatisfyRequestOffset(const uint32_t requestOffset) {
		if (requestOffsetVerify < requestOffset) {
			const auto padding = requestOffset - requestOffsetVerify;
			if (relativeOffset < padding) {
				const auto available = std::min<size_t>(destination.size_bytes(), padding);
				std::fill_n(destination.begin(), available, 0);
				destination = destination.subspan(available);
				relativeOffset = 0;

			} else
				relativeOffset -= padding;

		} else if (requestOffsetVerify > requestOffset)
			throw CorruptDataException("Duplicate read on same region");
	}

public:
	void Progress(
		const uint32_t requestOffset,
		uint32_t blockOffset
	) {
		const auto read = std::span(&readBuffer[0], static_cast<size_t>(Underlying.ReadStreamPartial(blockOffset, &readBuffer[0], readBuffer.size())));
		const auto& blockHeader = *reinterpret_cast<const SqData::BlockHeader*>(&readBuffer[0]);

		if (readBuffer.size() < sizeof blockHeader + blockHeader.CompressedSize) {
			readBuffer.resize(static_cast<uint16_t>(sizeof blockHeader + blockHeader.CompressedSize));
			Progress(requestOffset, blockOffset);
			return;
		}

		AttemptSatisfyRequestOffset(requestOffset);
		if (destination.empty())
			return;

		requestOffsetVerify += blockHeader.DecompressedSize;

		if (relativeOffset < blockHeader.DecompressedSize) {
			auto target = destination.subspan(0, std::min(destination.size_bytes(), static_cast<size_t>(blockHeader.DecompressedSize - relativeOffset)));
			if (blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed) {
				std::copy_n(&read[static_cast<size_t>(sizeof blockHeader + relativeOffset)], target.size(), target.begin());
			} else {
				auto decompressionTarget = target;
				if (relativeOffset) {
					zlibBuffer.resize(blockHeader.DecompressedSize);
					decompressionTarget = std::span(zlibBuffer);
				}

				if (sizeof blockHeader + blockHeader.CompressedSize > read.size_bytes())
					throw CorruptDataException("Failed to read block");

				InitializeZlib();
				zstream.next_in = &read[sizeof blockHeader];
				zstream.avail_in = blockHeader.CompressedSize;
				zstream.next_out = &decompressionTarget[0];
				zstream.avail_out = static_cast<uInt>(decompressionTarget.size());

				if (const auto res = inflate(&zstream, Z_FINISH);
					res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
					throw Utils::ZlibError(res);
				if (zstream.avail_out)
					throw CorruptDataException("Not enough data produced");

				if (relativeOffset)
					std::copy_n(&decompressionTarget[static_cast<size_t>(relativeOffset)],
						target.size_bytes(),
						target.begin());
			}
			destination = destination.subspan(target.size_bytes());

			relativeOffset = 0;
		} else
			relativeOffset -= blockHeader.DecompressedSize;
	}
};

uint64_t Sqex::Sqpack::EntryRawStream::BinaryStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	if (!length)
		return 0;

	auto it = static_cast<size_t>(std::distance(m_offsets.begin(), std::lower_bound(m_offsets.begin(), m_offsets.end(), static_cast<uint32_t>(offset))));
	if (it == m_offsets.size() || (it != m_offsets.size() && m_offsets[it] > offset))
		--it;

	ReadStreamState info{
		.Underlying = Underlying(),
		.destination = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
		.readBuffer = std::vector<uint8_t>(MaxBlockSize),
		.relativeOffset = offset - m_offsets[it],
		.requestOffsetVerify = m_offsets[it],
	};

	for (; it < m_offsets.size(); ++it) {
		info.Progress(m_offsets[it], m_blockOffsets[it]);
		if (info.destination.empty())
			break;
	}

	MaxBlockSize = info.readBuffer.size();
	return length - info.destination.size_bytes();
}

Sqex::Sqpack::EntryRawStream::TextureStreamDecoder::TextureStreamDecoder(const EntryRawStream* stream)
	: StreamDecoder(stream) {
	uint64_t readOffset = sizeof SqData::FileEntryHeader;
	const auto locators = Underlying().ReadStreamIntoVector<SqData::TextureBlockHeaderLocator>(readOffset, EntryHeader().BlockCountOrVersion);
	readOffset += std::span(locators).size_bytes();

	for (const auto& locator : locators) {
		Blocks.emplace_back(BlockInfo{
			.RequestOffset = 0,
			.BlockOffset = EntryHeader().HeaderSize + locator.FirstBlockOffset,
			.MipmapIndex = static_cast<uint16_t>(Blocks.size()),
			.RemainingDecompressedSize = locator.DecompressedSize,
			.RemainingBlockSizes = Underlying().ReadStreamIntoVector<uint16_t>(readOffset, locator.SubBlockCount),
			});
		readOffset += std::span(Blocks.back().RemainingBlockSizes).size_bytes();
	}

	Head.reserve(Align(sizeof Texture::Header + 8 * sizeof uint32_t));

	readOffset = EntryHeader().HeaderSize;
	Head.resize(sizeof Texture::Header);
	Underlying().ReadStream(readOffset, std::span(Head));
	const auto texHeader = *reinterpret_cast<const Texture::Header*>(&Head[0]);
	readOffset += sizeof texHeader;
	if (texHeader.MipmapCount != locators.size())
		throw CorruptDataException("MipmapCount != count(TextureBlockHeaderLocator)");

	Head.resize(sizeof Texture::Header + texHeader.MipmapCount * sizeof uint32_t);
	Underlying().ReadStream(readOffset, std::span(Head).subspan(sizeof Texture::Header));
	const auto mipmapOffsets = Underlying().ReadStreamIntoVector<uint32_t>(readOffset, texHeader.MipmapCount);
	if (sizeof texHeader + std::span(mipmapOffsets).size_bytes() > locators[0].FirstBlockOffset)
		throw CorruptDataException("Mipmap offset entries overlap the first block");

	if (!Blocks.empty()) {
		if (mipmapOffsets[0] < Head.size())
			throw CorruptDataException("First block overlaps header");
		Head.resize(mipmapOffsets[0], 0);
	}

	for (size_t i = 0; i < Blocks.size(); ++i)
		Blocks[i].RequestOffset = mipmapOffsets[i] - static_cast<uint32_t>(Head.size());
}

uint64_t Sqex::Sqpack::EntryRawStream::TextureStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	if (!length)
		return 0;

	ReadStreamState info{
		.Underlying = Underlying(),
		.destination = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
		.readBuffer = std::vector<uint8_t>(MaxBlockSize),
		.relativeOffset = offset,
	};

	if (info.relativeOffset < Head.size()) {
		const auto available = std::min(info.destination.size_bytes(), static_cast<size_t>(Head.size() - info.relativeOffset));
		const auto src = std::span(Head).subspan(static_cast<size_t>(info.relativeOffset), available);
		std::copy_n(src.begin(), available, info.destination.begin());
		info.destination = info.destination.subspan(available);
		info.relativeOffset = 0;
	} else
		info.relativeOffset -= Head.size();

	if (info.destination.empty() || Blocks.empty())
		return length - info.destination.size_bytes();

	auto it = std::lower_bound(Blocks.begin(), Blocks.end(), static_cast<uint32_t>(info.relativeOffset), [&](const BlockInfo& l, uint32_t r) {
		return l.RequestOffset < r;
	});
	if (it == Blocks.end() || (it != Blocks.end() && it != Blocks.begin() && it->RequestOffset > info.relativeOffset))
		--it;

	info.requestOffsetVerify = it->RequestOffset;
	info.relativeOffset -= info.requestOffsetVerify;

	while (it != Blocks.end()) {
		info.Progress(it->RequestOffset, it->BlockOffset);

		if (it->RemainingBlockSizes.empty()) {
			++it;
		} else {
			const auto& blockHeader = info.AsHeader();
			it = Blocks.emplace(++it, BlockInfo{
				.RequestOffset = it->RequestOffset + blockHeader.DecompressedSize,
				.BlockOffset = it->BlockOffset + it->RemainingBlockSizes.front(),
				.MipmapIndex = it->MipmapIndex,
				.RemainingDecompressedSize = it->RemainingDecompressedSize - blockHeader.DecompressedSize,
				.RemainingBlockSizes = std::move(it->RemainingBlockSizes),
				});
			it->RemainingBlockSizes.erase(it->RemainingBlockSizes.begin());
		}

		if (info.destination.empty())
			break;
	}

	MaxBlockSize = info.readBuffer.size();
	return length - info.destination.size_bytes();
}

Sqex::Sqpack::EntryRawStream::ModelStreamDecoder::ModelStreamDecoder(const EntryRawStream* stream)
	: StreamDecoder(stream) {
	uint64_t readOffset = sizeof SqData::FileEntryHeader;
	const auto locator = Underlying().ReadStream<SqData::ModelBlockLocator>(readOffset);
	const auto blockCount = static_cast<size_t>(locator.FirstBlockIndices.Index[2]) + locator.BlockCount.Index[2];

	readOffset += sizeof locator;
	for (const auto blockSize : Underlying().ReadStreamIntoVector<uint16_t>(readOffset, blockCount)) {
		Blocks.emplace_back(BlockInfo{
			.RequestOffset = 0,
			.BlockOffset = Blocks.empty() ? EntryHeader().HeaderSize.Value() : Blocks.back().BlockOffset + Blocks.back().PaddedChunkSize,
			.PaddedChunkSize = blockSize,
			.GroupIndex = UINT16_MAX,
			.GroupBlockIndex = 0,
			});
	}

	Head.resize(sizeof Model::Header);
	AsHeader() = {
		.Version = EntryHeader().BlockCountOrVersion,
		.VertexDeclarationCount = locator.VertexDeclarationCount,
		.MaterialCount = locator.MaterialCount,
		.LodCount = locator.LodCount,
		.EnableIndexBufferStreaming = locator.EnableIndexBufferStreaming,
		.EnableEdgeGeometry = locator.EnableEdgeGeometry,
		.Padding = locator.Padding,
	};

	if (Blocks.empty())
		return;

	for (uint16_t i = 0; i < 11; ++i) {
		if (!locator.BlockCount.EntryAt(i))
			continue;

		const size_t blockIndex = locator.FirstBlockIndices.EntryAt(i).Value();
		auto& firstBlock = Blocks[blockIndex];
		firstBlock.GroupIndex = i;
		firstBlock.GroupBlockIndex = 0;

		for (uint16_t j = 1; j < locator.BlockCount.EntryAt(i); ++j) {
			if (blockIndex + j >= blockCount)
				throw CorruptDataException("Out of bounds index information detected");

			auto& block = Blocks[blockIndex + j];
			if (block.GroupIndex != UINT16_MAX)
				throw CorruptDataException("Overlapping index information detected");
			block.GroupIndex = i;
			block.GroupBlockIndex = j;
		}
	}

	auto lastOffset = 0;
	for (auto it = Blocks.begin(); it != Blocks.end(); ++it) {
		SqData::BlockHeader blockHeader;
		Underlying().ReadStream(it->BlockOffset, &blockHeader, sizeof blockHeader);
		if (MaxBlockSize < sizeof blockHeader + blockHeader.CompressedSize)
			MaxBlockSize = static_cast<uint16_t>(sizeof blockHeader + blockHeader.CompressedSize);

		it->DecompressedSize = static_cast<uint16_t>(blockHeader.DecompressedSize);
		it->RequestOffset = lastOffset;
		lastOffset += it->DecompressedSize;
	}

	for (uint16_t i = locator.FirstBlockIndices.Stack.Value(), i_ = locator.FirstBlockIndices.Stack + locator.BlockCount.Stack; i < i_; ++i)
		AsHeader().StackSize += Blocks[i].DecompressedSize;
	for (uint16_t i = locator.FirstBlockIndices.Runtime.Value(), i_ = locator.FirstBlockIndices.Runtime + locator.BlockCount.Runtime; i < i_; ++i)
		AsHeader().RuntimeSize += Blocks[i].DecompressedSize;
	for (int j = 0; j < 3; ++j) {
		for (uint16_t i = locator.FirstBlockIndices.Vertex[j].Value(), i_ = locator.FirstBlockIndices.Vertex[j] + locator.BlockCount.Vertex[j]; i < i_; ++i)
			AsHeader().VertexSize[j] += Blocks[i].DecompressedSize;
		for (uint16_t i = locator.FirstBlockIndices.Index[j].Value(), i_ = locator.FirstBlockIndices.Index[j] + locator.BlockCount.Index[j]; i < i_; ++i)
			AsHeader().IndexSize[j] += Blocks[i].DecompressedSize;
		AsHeader().VertexOffset[j] = static_cast<uint32_t>(Head.size() + Blocks[locator.FirstBlockIndices.Vertex[j]].RequestOffset);
		AsHeader().IndexOffset[j] = static_cast<uint32_t>(Head.size() + Blocks[locator.FirstBlockIndices.Index[j]].RequestOffset);
	}
}

uint64_t Sqex::Sqpack::EntryRawStream::ModelStreamDecoder::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) {
	if (!length)
		return 0;

	ReadStreamState info{
		.Underlying = Underlying(),
		.destination = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
		.readBuffer = std::vector<uint8_t>(MaxBlockSize),
		.relativeOffset = offset,
	};

	if (info.relativeOffset < Head.size()) {
		const auto available = std::min(info.destination.size_bytes(), static_cast<size_t>(Head.size() - info.relativeOffset));
		const auto src = std::span(Head).subspan(info.relativeOffset, available);
		std::copy_n(src.begin(), available, info.destination.begin());
		info.destination = info.destination.subspan(available);
		info.relativeOffset = 0;
	} else
		info.relativeOffset -= Head.size();

	if (info.destination.empty() || Blocks.empty())
		return length - info.destination.size_bytes();

	auto it = std::lower_bound(Blocks.begin(), Blocks.end(), static_cast<uint32_t>(info.relativeOffset), [&](const BlockInfo& l, uint32_t r) {
		return l.RequestOffset < r;
	});
	if (it == Blocks.end() || (it != Blocks.end() && it != Blocks.begin() && it->RequestOffset > info.relativeOffset))
		--it;

	info.requestOffsetVerify = it->RequestOffset;
	info.relativeOffset -= info.requestOffsetVerify;

	for (; it != Blocks.end(); ++it) {
		info.Progress(it->RequestOffset, it->BlockOffset);
		if (info.destination.empty())
			break;
	}

	MaxBlockSize = info.readBuffer.size();
	return length - info.destination.size_bytes();
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
