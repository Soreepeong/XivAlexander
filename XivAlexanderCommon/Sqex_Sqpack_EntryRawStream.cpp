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
	if (it == m_offsets.end() || (it != m_offsets.end() && *it > offset))
		--it;

	relativeOffset -= *it;

	std::vector<uint8_t> blockBuffer(MaxBlockSize);
	std::vector<uint8_t> rawBuffer;
	for (const auto blockOffset : std::span(m_blockOffsets).subspan(std::distance(m_offsets.begin(), it))) {
		const auto read = std::span(&blockBuffer[0], static_cast<size_t>(Underlying().ReadStreamPartial(blockOffset, &blockBuffer[0], blockBuffer.size())));
		const auto& blockHeader = *reinterpret_cast<const SqData::BlockHeader*>(&blockBuffer[0]);

		if (relativeOffset >= blockHeader.DecompressedSize) {
			relativeOffset -= blockHeader.DecompressedSize;
			continue;
		}

		auto target = out.subspan(0, std::min(out.size_bytes(), static_cast<size_t>(blockHeader.DecompressedSize - relativeOffset)));
		if (blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed) {
			std::copy_n(&read[static_cast<size_t>(sizeof blockHeader + relativeOffset)], target.size(), target.begin());
		} else {
			auto decompressionTarget = target;
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
					target.size_bytes(),
					target.begin());
		}
		out = out.subspan(target.size_bytes());

		if (out.empty())
			return length;

		relativeOffset = 0;
	}

	return length - out.size_bytes();
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
			.RemainingBlocksSize = locator.TotalSize,
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

	auto relativeOffset = offset;
	auto out = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));

	if (relativeOffset < Head.size()) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(Head.size() - relativeOffset));
		const auto src = std::span(Head).subspan(static_cast<size_t>(relativeOffset), available);
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;
	} else
		relativeOffset -= Head.size();

	if (out.empty() || Blocks.empty())
		return length - out.size_bytes();

	auto it = std::lower_bound(Blocks.begin(), Blocks.end(), static_cast<uint32_t>(relativeOffset), [&](const BlockInfo& l, uint32_t r) {
		return l.RequestOffset < r;
	});
	if (it == Blocks.end() || (it != Blocks.end() && it != Blocks.begin() && it->RequestOffset > relativeOffset))
		--it;

	auto currentRequestOffset = it->RequestOffset;
	relativeOffset -= currentRequestOffset;

	std::vector<uint8_t> blockBuffer(MaxBlockSize);
	std::vector<uint8_t> rawBuffer;
	while (it != Blocks.end()) {
		const auto read = std::span(&blockBuffer[0], static_cast<size_t>(Underlying().ReadStreamPartial(it->BlockOffset, &blockBuffer[0], blockBuffer.size())));
		const auto& blockHeader = *reinterpret_cast<const SqData::BlockHeader*>(&blockBuffer[0]);
		const auto blockDataSize = blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed ? blockHeader.DecompressedSize : blockHeader.CompressedSize;

		if (MaxBlockSize < sizeof blockHeader + blockHeader.CompressedSize) {
			MaxBlockSize = static_cast<uint16_t>(sizeof blockHeader + blockHeader.CompressedSize);
			blockBuffer.resize(MaxBlockSize);
			continue;
		}

		if (currentRequestOffset < it->RequestOffset) {
			const auto padding = it->RequestOffset - currentRequestOffset;
			if (relativeOffset < padding) {
				const auto available = std::min<size_t>(out.size_bytes(), padding);
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;
			} else
				relativeOffset -= padding;

		} else if (currentRequestOffset > it->RequestOffset)
			throw CorruptDataException("Duplicate read on same region");
		currentRequestOffset += blockHeader.DecompressedSize;

		if (relativeOffset < blockHeader.DecompressedSize) {
			auto target = out.subspan(0, std::min(out.size_bytes(), static_cast<size_t>(blockHeader.DecompressedSize - relativeOffset)));
			if (!target.empty()) {
				if (blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed) {
					std::copy_n(&read[static_cast<size_t>(sizeof blockHeader + relativeOffset)], target.size(), target.begin());
				} else {
					auto decompressionTarget = target;
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
							target.size_bytes(),
							target.begin());
				}
			}
			out = out.subspan(target.size_bytes());

			relativeOffset = 0;
		} else
			relativeOffset -= blockHeader.DecompressedSize;

		if (it->RemainingBlockSizes.empty()) {
			++it;
		} else {
			it = Blocks.emplace(++it, BlockInfo{
				.RequestOffset = it->RequestOffset + blockHeader.DecompressedSize,
				.BlockOffset = it->BlockOffset + it->RemainingBlockSizes.front(),
				.MipmapIndex = it->MipmapIndex,
				.RemainingBlocksSize = static_cast<uint32_t>(it->RemainingBlocksSize - sizeof blockHeader - blockDataSize),
				.RemainingDecompressedSize = it->RemainingDecompressedSize - blockHeader.DecompressedSize,
				.RemainingBlockSizes = std::move(it->RemainingBlockSizes),
				});
			it->RemainingBlockSizes.erase(it->RemainingBlockSizes.begin());
		}

		if (out.empty())
			return length;
	}

	return length - out.size_bytes();
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
		// .StackSize = locator.AlignedDecompressedSizes.Stack,
		// .RuntimeSize = locator.AlignedDecompressedSizes.Runtime,
		.VertexDeclarationCount = locator.VertexDeclarationCount,
		.MaterialCount = locator.MaterialCount,
		// .VertexOffset = {locator.FirstBlockOffsets.Vertex[0], locator.FirstBlockOffsets.Vertex[1], locator.FirstBlockOffsets.Vertex[2]},
		// .IndexOffset = {locator.FirstBlockOffsets.Index[0], locator.FirstBlockOffsets.Index[1], locator.FirstBlockOffsets.Index[2]},
		// .VertexSize = {locator.AlignedDecompressedSizes.Vertex[0], locator.AlignedDecompressedSizes.Vertex[1], locator.AlignedDecompressedSizes.Vertex[2]},
		// .IndexSize = {locator.AlignedDecompressedSizes.Index[0], locator.AlignedDecompressedSizes.Index[1], locator.AlignedDecompressedSizes.Index[2]},
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

	auto relativeOffset = offset;
	auto out = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));

	if (relativeOffset < Head.size()) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(Head.size() - relativeOffset));
		const auto src = std::span(Head).subspan(relativeOffset, available);
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;
	} else
		relativeOffset -= Head.size();

	if (out.empty() || Blocks.empty())
		return length - out.size_bytes();

	auto it = std::lower_bound(Blocks.begin(), Blocks.end(), static_cast<uint32_t>(relativeOffset), [&](const BlockInfo& l, uint32_t r) {
		return l.RequestOffset < r;
	});
	if (it == Blocks.end() || (it != Blocks.end() && it != Blocks.begin() && it->RequestOffset > relativeOffset))
		--it;

	auto currentRequestOffset = it->RequestOffset;
	relativeOffset -= currentRequestOffset;

	std::vector<uint8_t> blockBuffer(MaxBlockSize);
	std::vector<uint8_t> rawBuffer;
	while (it != Blocks.end()) {
		const auto read = std::span(&blockBuffer[0], static_cast<size_t>(Underlying().ReadStreamPartial(it->BlockOffset, &blockBuffer[0], blockBuffer.size())));
		const auto& blockHeader = *reinterpret_cast<const SqData::BlockHeader*>(&blockBuffer[0]);

		if (MaxBlockSize < sizeof blockHeader + blockHeader.CompressedSize) {
			MaxBlockSize = static_cast<uint16_t>(sizeof blockHeader + blockHeader.CompressedSize);
			blockBuffer.resize(MaxBlockSize);
			continue;
		}

		if (currentRequestOffset < it->RequestOffset) {
			const auto padding = it->RequestOffset - currentRequestOffset;
			if (relativeOffset < padding) {
				const auto available = std::min<size_t>(out.size_bytes(), padding);
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length;
			} else
				relativeOffset -= padding;

		} else if (currentRequestOffset > it->RequestOffset)
			throw CorruptDataException("Duplicate read on same region");
		currentRequestOffset += blockHeader.DecompressedSize;

		if (relativeOffset < blockHeader.DecompressedSize) {
			auto target = out.subspan(0, std::min(out.size_bytes(), static_cast<size_t>(blockHeader.DecompressedSize - relativeOffset)));
			if (!target.empty()) {
				if (blockHeader.CompressedSize == SqData::BlockHeader::CompressedSizeNotCompressed) {
					std::copy_n(&read[static_cast<size_t>(sizeof blockHeader + relativeOffset)], target.size(), target.begin());
				} else {
					auto decompressionTarget = target;
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
							target.size_bytes(),
							target.begin());
				}
			}
			out = out.subspan(target.size_bytes());
			if (out.empty())
				return length;

			relativeOffset = 0;
		} else
			relativeOffset -= blockHeader.DecompressedSize;

		++it;
	}

	return length - out.size_bytes();
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
