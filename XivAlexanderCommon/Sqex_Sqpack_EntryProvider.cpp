#include "pch.h"
#include "Sqex_Sqpack_EntryProvider.h"

#include "Sqex_Model.h"
#include "XaZlib.h"

uint64_t Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_header;

	if (const auto afterHeaderPad = m_header.HeaderSize - sizeof m_header;
		relativeOffset < afterHeaderPad) {
		const auto available = std::min(out.size_bytes(), afterHeaderPad);
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= afterHeaderPad;

	if (const auto dataSize = m_stream ? m_stream->StreamSize() : 0) {
		if (relativeOffset < dataSize) {
			const auto available = std::min(out.size_bytes(), dataSize - relativeOffset);
			m_stream->ReadStream(relativeOffset, &out[0], available);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= dataSize;
	}

	return length - out.size_bytes();
}

const Sqex::Sqpack::EmptyOrObfuscatedEntryProvider& Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::Instance() {
	static const EmptyOrObfuscatedEntryProvider s_instance{ EntryPathSpec() };
	return s_instance;
}

Sqex::Sqpack::LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider(EntryPathSpec spec, std::filesystem::path path, bool openImmediately, int compressionLevel)
	: EntryProvider(std::move(spec))
	, m_path(std::move(path))
	, m_stream(std::make_shared<FileRandomAccessStream>(m_path, 0, UINT64_MAX, openImmediately))
	, m_initializationMutex(std::make_unique<std::mutex>())
	, m_originalSize(m_stream->StreamSize())
	, m_compressionLevel(compressionLevel) {
}

Sqex::Sqpack::LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider(EntryPathSpec spec, std::shared_ptr<const RandomAccessStream> stream, int compressionLevel)
	: EntryProvider(std::move(spec))
	, m_path()
	, m_stream(std::move(stream))
	, m_initializationMutex(std::make_unique<std::mutex>())
	, m_originalSize(m_stream->StreamSize())
	, m_compressionLevel(compressionLevel) {
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::StreamSize() const {
	if (const auto estimate = MaxPossibleStreamSize();
		estimate != UINT64_MAX)
		return estimate;

	const_cast<LazyFileOpeningEntryProvider*>(this)->Resolve();
	return StreamSize(*m_stream);
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	const_cast<LazyFileOpeningEntryProvider*>(this)->Resolve();
	const auto size = StreamSize(*m_stream);
	const auto estimate = MaxPossibleStreamSize();
	if (estimate == size || offset + length <= size)
		return ReadStreamPartial(*m_stream, offset, buf, length);

	if (offset >= estimate)
		return 0;
	if (offset + length > estimate)
		length = estimate - offset;

	auto target = std::span(static_cast<char*>(buf), static_cast<size_t>(length));
	if (offset < size) {
		const auto read = static_cast<size_t>(ReadStreamPartial(*m_stream, offset, &target[0], std::min<uint64_t>(size - offset, target.size())));
		target = target.subspan(read);
	}
	// size ... estimate
	const auto remaining = static_cast<size_t>(std::min<uint64_t>(estimate - size, target.size()));
	std::ranges::fill(target.subspan(0, remaining), 0);
	return length - (target.size() - remaining);
}

void Sqex::Sqpack::LazyFileOpeningEntryProvider::Resolve() {
	if (!m_initializationMutex)
		return;

	// TODO: Is it legal to copy from a shared_ptr that is concurrently being set to null?
	const auto mtx = m_initializationMutex;
	if (!mtx)
		return;

	const auto lock = std::lock_guard(*mtx);
	if (!m_initializationMutex)
		return;

	Initialize(*m_stream);

	m_initializationMutex = nullptr;
}

void Sqex::Sqpack::OnTheFlyBinaryEntryProvider::Initialize(const RandomAccessStream& stream) {
	const auto blockCount = Align<uint64_t, uint32_t>(m_originalSize, BlockDataSize).Count;
	m_header = {
		.HeaderSize = static_cast<uint32_t>(sizeof m_header + blockCount * sizeof SqData::BlockHeaderLocator),
		.Type = SqData::FileEntryType::Binary,
		.DecompressedSize = static_cast<uint32_t>(m_originalSize),
		.BlockCountOrVersion = blockCount,
	};
	const auto align = Align(m_header.HeaderSize);
	m_padBeforeData = align.Pad;
	m_header.HeaderSize = align;
	m_header.SetSpaceUnits(MaxPossibleStreamSize());
}

uint64_t Sqex::Sqpack::OnTheFlyBinaryEntryProvider::MaxPossibleStreamSize() const {
	const auto blockCount = Align<uint64_t>(m_originalSize, BlockDataSize).Count;
	const auto headerSize = Align(sizeof m_header + blockCount * sizeof SqData::BlockHeaderLocator).Alloc;
	return headerSize + blockCount * BlockSize;
}

uint64_t Sqex::Sqpack::OnTheFlyBinaryEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_header;

	if (relativeOffset < m_header.BlockCountOrVersion * sizeof SqData::BlockHeaderLocator) {
		auto i = relativeOffset / sizeof SqData::BlockHeaderLocator;
		relativeOffset -= i * sizeof SqData::BlockHeaderLocator;
		for (; i < m_header.BlockCountOrVersion; ++i) {
			const auto decompressedDataSize = i == m_header.BlockCountOrVersion - 1 ? m_header.DecompressedSize % BlockDataSize : BlockDataSize;
			const auto locator = SqData::BlockHeaderLocator{
				static_cast<uint32_t>(i * BlockSize),
				static_cast<uint16_t>(BlockSize),
				static_cast<uint16_t>(decompressedDataSize)
			};

			if (relativeOffset < sizeof locator) {
				const auto src = std::span(reinterpret_cast<const char*>(&locator), sizeof locator)
					.subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= sizeof locator;
		}
	} else
		relativeOffset -= 1ULL * m_header.BlockCountOrVersion * sizeof SqData::BlockHeaderLocator;

	if (relativeOffset < m_padBeforeData) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(m_padBeforeData - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= m_padBeforeData;

	if (static_cast<uint32_t>(relativeOffset) < m_header.BlockCountOrVersion * BlockSize) {
		auto i = relativeOffset / BlockSize;
		relativeOffset -= i * BlockSize;
		for (; i < m_header.BlockCountOrVersion; ++i) {
			const auto decompressedSize = i == m_header.BlockCountOrVersion - 1 ? m_header.DecompressedSize % BlockDataSize : BlockDataSize;

			if (relativeOffset < sizeof SqData::BlockHeader) {
				const auto header = SqData::BlockHeader{
					.HeaderSize = sizeof SqData::BlockHeader,
					.Version = 0,
					.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
					.DecompressedSize = decompressedSize,
				};
				const auto src = std::span(reinterpret_cast<const char*>(&header), sizeof header)
					.subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= sizeof SqData::BlockHeader;

			if (relativeOffset < decompressedSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
				stream.ReadStream(i * BlockDataSize + relativeOffset, &out[0], available);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= decompressedSize;

			if (const auto padSize = BlockDataSize + BlockPadSize - decompressedSize; relativeOffset < padSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(static_cast<size_t>(available));
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= padSize;
		}
	}

	return length - out.size_bytes();
}

void Sqex::Sqpack::MemoryBinaryEntryProvider::Initialize(const RandomAccessStream& stream) {
	const auto rawSize = static_cast<uint32_t>(stream.StreamSize());
	SqData::FileEntryHeader entryHeader = {
		.HeaderSize = sizeof entryHeader,
		.Type = SqData::FileEntryType::Binary,
		.DecompressedSize = rawSize,
		.BlockCountOrVersion = 0,
	};

	std::optional<ZlibReusableDeflater> deflater;
	if (m_compressionLevel)
		deflater.emplace(m_compressionLevel, Z_DEFLATED, -15);
	std::vector<uint8_t> entryBody;
	entryBody.reserve(rawSize);

	std::vector<SqData::BlockHeaderLocator> locators;
	std::vector<uint8_t> buf(BlockDataSize);
	for (uint32_t i = 0; i < rawSize; i += BlockDataSize) {
		const auto sourceBuf = std::span(buf).subspan(0, std::min<uint32_t>(BlockDataSize, rawSize - i));
		stream.ReadStream(i, sourceBuf);
		if (deflater)
			deflater->Deflate(sourceBuf);
		const auto useCompressed = deflater && deflater->Result().size() < sourceBuf.size();
		const auto targetBuf = useCompressed ? deflater->Result() : sourceBuf;

		SqData::BlockHeader header{
			.HeaderSize = sizeof SqData::BlockHeader,
			.Version = 0,
			.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : SqData::BlockHeader::CompressedSizeNotCompressed,
			.DecompressedSize = static_cast<uint32_t>(sourceBuf.size()),
		};
		const auto alignmentInfo = Align(sizeof header + targetBuf.size());

		locators.emplace_back(SqData::BlockHeaderLocator{
			locators.empty() ? 0 : locators.back().BlockSize + locators.back().Offset,
			static_cast<uint16_t>(alignmentInfo.Alloc),
			static_cast<uint16_t>(sourceBuf.size())
			});

		entryBody.resize(entryBody.size() + alignmentInfo.Alloc);

		auto ptr = entryBody.end() - static_cast<SSIZE_T>(alignmentInfo.Alloc);
		ptr = std::copy_n(reinterpret_cast<uint8_t*>(&header), sizeof header, ptr);
		ptr = std::copy(targetBuf.begin(), targetBuf.end(), ptr);
		std::fill_n(ptr, alignmentInfo.Pad, 0);
	}

	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(locators.size());
	entryHeader.HeaderSize = static_cast<uint32_t>(Align(entryHeader.HeaderSize + std::span(locators).size_bytes()));
	m_data.reserve(Align(entryHeader.HeaderSize + entryBody.size()));
	m_data.insert(m_data.end(), reinterpret_cast<char*>(&entryHeader), reinterpret_cast<char*>(&entryHeader + 1));
	if (!locators.empty()) {
		m_data.insert(m_data.end(), reinterpret_cast<char*>(&locators.front()), reinterpret_cast<char*>(&locators.back() + 1));
		m_data.resize(entryHeader.HeaderSize, 0);
		m_data.insert(m_data.end(), entryBody.begin(), entryBody.end());
	} else
		m_data.resize(entryHeader.HeaderSize, 0);

	m_data.resize(Align(m_data.size()));
	reinterpret_cast<SqData::FileEntryHeader*>(&m_data[0])->SetSpaceUnits(m_data.size());
}

uint64_t Sqex::Sqpack::MemoryBinaryEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	const auto available = static_cast<size_t>(std::min(length, m_data.size() - offset));
	if (!available)
		return 0;

	memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
	return available;
}

void Sqex::Sqpack::OnTheFlyModelEntryProvider::Initialize(const RandomAccessStream& stream) {
	Model::Header header;
	stream.ReadStream(0, &header, sizeof header);

	m_header.Entry.Type = SqData::FileEntryType::Model;
	m_header.Entry.DecompressedSize = static_cast<uint32_t>(stream.StreamSize());
	m_header.Entry.BlockCountOrVersion = header.Version;

	m_header.Model.VertexDeclarationCount = header.VertexDeclarationCount;
	m_header.Model.MaterialCount = header.MaterialCount;
	m_header.Model.LodCount = header.LodCount;
	m_header.Model.EnableIndexBufferStreaming = header.EnableIndexBufferStreaming;
	m_header.Model.EnableEdgeGeometry = header.EnableEdgeGeometry;

	const auto getNextBlockOffset = [&]() {
		return m_paddedBlockSizes.empty() ? 0U : m_blockOffsets.back() + m_paddedBlockSizes.back();
	};

	auto baseFileOffset = static_cast<uint32_t>(sizeof header);
	const auto generateSet = [&](uint32_t size) {
		const auto alignedDecompressedSize = Align(size).Alloc;
		const auto alignedBlock = Align<uint32_t, uint16_t>(size, BlockDataSize);
		const auto firstBlockOffset = size ? getNextBlockOffset() : 0;
		const auto firstBlockIndex = static_cast<uint16_t>(m_blockOffsets.size());
		alignedBlock.IterateChunked([&](auto, uint32_t offset, uint32_t size) {
			m_blockOffsets.push_back(getNextBlockOffset());
			m_blockDataSizes.push_back(size);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + size)));
			m_actualFileOffsets.push_back(offset);
			}, baseFileOffset);
		const auto chunkSize = size ? getNextBlockOffset() - firstBlockOffset : 0;
		baseFileOffset += size;

		return std::make_tuple(
			alignedDecompressedSize,
			alignedBlock.Count,
			firstBlockOffset,
			firstBlockIndex,
			chunkSize
		);
	};

	std::tie(m_header.Model.AlignedDecompressedSizes.Stack,
		m_header.Model.BlockCount.Stack,
		m_header.Model.FirstBlockOffsets.Stack,
		m_header.Model.FirstBlockIndices.Stack,
		m_header.Model.ChunkSizes.Stack) = generateSet(header.StackSize);

	std::tie(m_header.Model.AlignedDecompressedSizes.Runtime,
		m_header.Model.BlockCount.Runtime,
		m_header.Model.FirstBlockOffsets.Runtime,
		m_header.Model.FirstBlockIndices.Runtime,
		m_header.Model.ChunkSizes.Runtime) = generateSet(header.RuntimeSize);

	for (size_t i = 0; i < 3; i++) {
		std::tie(m_header.Model.AlignedDecompressedSizes.Vertex[i],
			m_header.Model.BlockCount.Vertex[i],
			m_header.Model.FirstBlockOffsets.Vertex[i],
			m_header.Model.FirstBlockIndices.Vertex[i],
			m_header.Model.ChunkSizes.Vertex[i]) = generateSet(header.VertexSize[i]);

		std::tie(m_header.Model.AlignedDecompressedSizes.EdgeGeometryVertex[i],
			m_header.Model.BlockCount.EdgeGeometryVertex[i],
			m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i],
			m_header.Model.FirstBlockIndices.EdgeGeometryVertex[i],
			m_header.Model.ChunkSizes.EdgeGeometryVertex[i]) = generateSet(header.IndexOffset[i] - baseFileOffset);

		std::tie(m_header.Model.AlignedDecompressedSizes.Index[i],
			m_header.Model.BlockCount.Index[i],
			m_header.Model.FirstBlockOffsets.Index[i],
			m_header.Model.FirstBlockIndices.Index[i],
			m_header.Model.ChunkSizes.Index[i]) = generateSet(header.IndexSize[i]);
	}

	if (baseFileOffset > m_header.Entry.DecompressedSize)
		throw std::runtime_error("Bad model file (incomplete data)");

	m_header.Entry.HeaderSize = Align(static_cast<uint32_t>(sizeof m_header + std::span(m_blockDataSizes).size_bytes()));
	m_header.Entry.SetSpaceUnits(MaxPossibleStreamSize());
}

uint64_t Sqex::Sqpack::OnTheFlyModelEntryProvider::MaxPossibleStreamSize() const {
	const auto blockCount = 11 + Align<uint64_t>(m_originalSize, BlockDataSize).Count;
	const auto headerSize = Align(sizeof m_header + blockCount * sizeof m_blockDataSizes[0]).Alloc;
	return headerSize + blockCount * BlockSize;
}

uint64_t Sqex::Sqpack::OnTheFlyModelEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= sizeof m_header;

	if (const auto srcTyped = std::span(m_paddedBlockSizes);
		relativeOffset < srcTyped.size_bytes()) {
		const auto src = std::span(reinterpret_cast<const char*>(srcTyped.data()), srcTyped.size_bytes())
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (const auto padBeforeBlocks = Align(sizeof ModelEntryHeader + std::span(m_paddedBlockSizes).size_bytes()).Pad;
		relativeOffset < padBeforeBlocks) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(padBeforeBlocks - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= padBeforeBlocks;

	auto it = std::ranges::lower_bound(m_blockOffsets,
		static_cast<uint32_t>(relativeOffset),
		[&](uint32_t l, uint32_t r) {
			return l < r;
		});

	if (it == m_blockOffsets.end())
		--it;
	while (*it > relativeOffset) {
		if (it == m_blockOffsets.begin()) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(*it - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
			break;
		} else
			--it;
	}

	relativeOffset -= *it;

	for (auto i = it - m_blockOffsets.begin(); it != m_blockOffsets.end(); ++it, ++i) {
		if (relativeOffset < sizeof SqData::BlockHeader) {
			const auto header = SqData::BlockHeader{
				.HeaderSize = sizeof SqData::BlockHeader,
				.Version = 0,
				.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
				.DecompressedSize = m_blockDataSizes[i],
			};
			const auto src = std::span(reinterpret_cast<const char*>(&header), sizeof header)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= sizeof SqData::BlockHeader;

		if (relativeOffset < m_blockDataSizes[i]) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(m_blockDataSizes[i] - relativeOffset));
			stream.ReadStream(m_actualFileOffsets[i] + relativeOffset, &out[0], available);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= m_blockDataSizes[i];

		if (const auto padSize = m_paddedBlockSizes[i] - m_blockDataSizes[i] - sizeof SqData::BlockHeader;
			relativeOffset < padSize) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
			relativeOffset = 0;

			if (out.empty()) return length;
		} else
			relativeOffset -= padSize;
	}

	if (!out.empty()) {
		const auto actualDataSize = m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back());
		const auto endPadSize = MaxPossibleStreamSize() - actualDataSize;
		if (relativeOffset < endPadSize) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
		}
	}

	return length - out.size_bytes();
}

void Sqex::Sqpack::OnTheFlyTextureEntryProvider::Initialize(const RandomAccessStream& stream) {
	auto entryHeader = SqData::FileEntryHeader{
		.HeaderSize = sizeof SqData::FileEntryHeader,
		.Type = SqData::FileEntryType::Texture,
		.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
	};

	m_texHeaderBytes.resize(sizeof Texture::Header);
	stream.ReadStream(0, std::span(m_texHeaderBytes));
	entryHeader.BlockCountOrVersion = AsTexHeader().MipmapCount;

	m_texHeaderBytes.resize(sizeof Texture::Header + AsTexHeader().MipmapCount * sizeof uint32_t);
	stream.ReadStream(sizeof Texture::Header, std::span(
		reinterpret_cast<uint32_t*>(&m_texHeaderBytes[sizeof Texture::Header]),
		AsTexHeader().MipmapCount.Value()
	));

	m_texHeaderBytes.resize(AsMipmapOffsets()[0]);
	stream.ReadStream(0, std::span(m_texHeaderBytes).subspan(0, AsMipmapOffsets()[0]));

	const auto mipmapOffsets = AsMipmapOffsets();
	for (const auto offset : mipmapOffsets) {
		if (!m_mipmapSizes.empty())
			m_mipmapSizes.back() = offset - m_mipmapSizes.back();
		m_mipmapSizes.push_back(offset);
	}
	m_mipmapSizes.back() = entryHeader.DecompressedSize - m_mipmapSizes.back();

	uint32_t blockOffsetCounter = 0;
	for (size_t i = 0; i < mipmapOffsets.size(); ++i) {
		const auto mipmapSize = m_mipmapSizes[i];
		const auto subBlockCount = (mipmapSize + BlockDataSize - 1) / BlockDataSize;
		SqData::TextureBlockHeaderLocator loc{
			.FirstBlockOffset = blockOffsetCounter,
			.TotalSize = 0,
			.DecompressedSize = mipmapSize,
			.FirstSubBlockIndex = i == 0 ? 0 : m_blockLocators.back().FirstSubBlockIndex + m_blockLocators.back().SubBlockCount,
			.SubBlockCount = subBlockCount,
		};
		for (int j = loc.SubBlockCount - 1; j >= 0; --j) {
			const auto alignmentInfo = Align(sizeof SqData::BlockHeader + (j ? BlockDataSize : mipmapSize % BlockDataSize));
			m_subBlockSizes.push_back(static_cast<uint16_t>(alignmentInfo.Alloc));
			blockOffsetCounter += m_subBlockSizes.back();
			loc.TotalSize += m_subBlockSizes.back();
		}

		m_blockLocators.emplace_back(loc);
	}
	for (auto& loc : m_blockLocators) {
		loc.FirstBlockOffset = loc.FirstBlockOffset + static_cast<uint32_t>(std::span(m_texHeaderBytes).size_bytes());
	}

	entryHeader.HeaderSize = entryHeader.HeaderSize + static_cast<uint32_t>(
		std::span(m_blockLocators).size_bytes() +
		std::span(m_subBlockSizes).size_bytes());
	entryHeader.HeaderSize = Sqex::Align(entryHeader.HeaderSize).Alloc;

	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&entryHeader),
		reinterpret_cast<char*>(&entryHeader + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_blockLocators.front()),
		reinterpret_cast<char*>(&m_blockLocators.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_subBlockSizes.front()),
		reinterpret_cast<char*>(&m_subBlockSizes.back() + 1));
	m_mergedHeader.resize(entryHeader.HeaderSize);
	m_mergedHeader.insert(m_mergedHeader.end(),
		m_texHeaderBytes.begin(),
		m_texHeaderBytes.end());

	m_size += m_mergedHeader.size();
	for (const auto mipmapSize : m_mipmapSizes) {
		m_size += (size_t{} + mipmapSize + BlockDataSize - 1) / BlockDataSize * sizeof SqData::BlockHeader + mipmapSize;
	}

	reinterpret_cast<SqData::FileEntryHeader*>(&m_mergedHeader[0])->SetSpaceUnits(m_size);
}

uint64_t Sqex::Sqpack::OnTheFlyTextureEntryProvider::MaxPossibleStreamSize() const {
	const auto blockCount = MaxMipmapCountPerTexture + Align<uint64_t>(m_originalSize, BlockDataSize).Count;
	const auto headerSize = sizeof SqData::FileEntryHeader
		+ blockCount * sizeof m_subBlockSizes[0]
		+ MaxMipmapCountPerTexture * sizeof m_blockLocators[0];
	return headerSize + blockCount * BlockSize;
}

uint64_t Sqex::Sqpack::OnTheFlyTextureEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < m_mergedHeader.size()) {
		const auto src = std::span(m_mergedHeader)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty()) return length;
	} else
		relativeOffset -= m_mergedHeader.size();

	if (relativeOffset < m_size - m_mergedHeader.size()) {
		relativeOffset += std::span(m_texHeaderBytes).size_bytes();
		auto it = std::lower_bound(m_blockLocators.begin(), m_blockLocators.end(),
			SqData::TextureBlockHeaderLocator{ .FirstBlockOffset = static_cast<uint32_t>(relativeOffset) },
			[&](const SqData::TextureBlockHeaderLocator& l, const SqData::TextureBlockHeaderLocator& r) {
				return l.FirstBlockOffset < r.FirstBlockOffset;
			});

		if (it == m_blockLocators.end())
			--it;
		while (it->FirstBlockOffset > relativeOffset) {
			if (it == m_blockLocators.begin()) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(it->FirstBlockOffset - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
				break;
			} else
				--it;
		}

		relativeOffset -= it->FirstBlockOffset;

		for (; it != m_blockLocators.end(); ++it) {
			const auto blockIndex = it - m_blockLocators.begin();
			auto j = relativeOffset / (sizeof SqData::BlockHeader + BlockDataSize);
			relativeOffset -= j * (sizeof SqData::BlockHeader + BlockDataSize);
			for (; j < it->SubBlockCount; ++j) {
				const auto decompressedSize = j == it->SubBlockCount - 1 ? m_mipmapSizes[blockIndex] % BlockDataSize : BlockDataSize;
				const auto pad = Align(sizeof SqData::BlockHeader + decompressedSize).Pad;

				if (relativeOffset < sizeof SqData::BlockHeader) {
					const auto header = SqData::BlockHeader{
						.HeaderSize = sizeof SqData::BlockHeader,
						.Version = 0,
						.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
						.DecompressedSize = decompressedSize,
					};
					const auto src = std::span(reinterpret_cast<const char*>(&header), sizeof header)
						.subspan(static_cast<size_t>(relativeOffset));
					const auto available = std::min(out.size_bytes(), src.size_bytes());
					std::copy_n(src.begin(), available, out.begin());
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= sizeof SqData::BlockHeader;

				if (relativeOffset < decompressedSize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
					stream.ReadStream(AsMipmapOffsets()[blockIndex] + j * BlockDataSize + relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= decompressedSize;

				if (relativeOffset < pad) {
					const auto available = std::min(out.size_bytes(), pad);
					std::fill_n(&out[0], available, 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else {
					relativeOffset -= pad;
				}
			}
		}
	}

	if (const auto endPadSize = StreamSize() - StreamSize(stream); relativeOffset < endPadSize) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(static_cast<size_t>(available));
	}

	return length - out.size_bytes();
}

void Sqex::Sqpack::MemoryTextureEntryProvider::Initialize(const RandomAccessStream& stream) {
	std::vector<SqData::TextureBlockHeaderLocator> blockLocators;
	std::vector<uint8_t> readBuffer(BlockDataSize);
	std::vector<uint16_t> subBlockSizes;
	std::vector<uint8_t> texHeaderBytes;

	auto AsTexHeader = [&]() { return *reinterpret_cast<const Texture::Header*>(&texHeaderBytes[0]); };
	auto AsMipmapOffsets = [&]() { return std::span(reinterpret_cast<const uint32_t*>(&texHeaderBytes[sizeof Texture::Header]), AsTexHeader().MipmapCount); };

	auto entryHeader = SqData::FileEntryHeader{
		.Type = SqData::FileEntryType::Texture,
		.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
	};

	texHeaderBytes.resize(sizeof Texture::Header);
	stream.ReadStream(0, std::span(texHeaderBytes));

	texHeaderBytes.resize(sizeof Texture::Header + AsTexHeader().MipmapCount * sizeof uint32_t);
	stream.ReadStream(sizeof Texture::Header, std::span(
		reinterpret_cast<uint32_t*>(&texHeaderBytes[sizeof Texture::Header]),
		AsTexHeader().MipmapCount.Value()
	));

	texHeaderBytes.resize(AsMipmapOffsets()[0]);
	stream.ReadStream(0, std::span(texHeaderBytes).subspan(0, AsMipmapOffsets()[0]));

	std::vector<uint32_t> mipmapOffsets(AsMipmapOffsets().begin(), AsMipmapOffsets().end());;
	std::vector<uint32_t> mipmapSizes(mipmapOffsets.size());
	const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (size_t{} + mipmapOffsets[1] - mipmapOffsets[0]) / Texture::RawDataLength(AsTexHeader(), 0);
	for (size_t i = 0; i < mipmapOffsets.size(); ++i)
		mipmapSizes[i] = static_cast<uint32_t>(Texture::RawDataLength(AsTexHeader(), i));
	
	while (mipmapOffsets.empty() || mipmapOffsets.back() + mipmapSizes.back() * repeatCount < entryHeader.DecompressedSize) {
		for (size_t i = 0, i_ = AsTexHeader().MipmapCount; i < i_; ++i) {
			mipmapOffsets.push_back(mipmapOffsets.back() + mipmapSizes.back());
			mipmapSizes.push_back(static_cast<uint32_t>(Texture::RawDataLength(AsTexHeader(), i)));
		}
	}

	std::optional<ZlibReusableDeflater> deflater;
	if (m_compressionLevel)
		deflater.emplace(m_compressionLevel, Z_DEFLATED, -15);
	std::vector<uint8_t> entryBody;
	entryBody.reserve(static_cast<SSIZE_T>(stream.StreamSize()));

	auto blockOffsetCounter = static_cast<uint32_t>(std::span(texHeaderBytes).size_bytes());
	for (size_t i = 0; i < mipmapOffsets.size(); ++i) {
		uint32_t maxMipmapSize = 0;

		const auto minSize = std::max(4U, static_cast<uint32_t>(Texture::RawDataLength(AsTexHeader().Type, 1, 1, AsTexHeader().Layers, i)));
		if (mipmapSizes[i] > minSize) {
			for (size_t repeatI = 0; repeatI < repeatCount; repeatI++) {
				size_t offset = mipmapOffsets[i] + mipmapSizes[i] * repeatI;
				auto mipmapSize = mipmapSizes[i];
				readBuffer.resize(mipmapSize);
				stream.ReadStream(offset, std::span(readBuffer));
				for (auto nextSize = mipmapSize;; mipmapSize = nextSize) {
					nextSize /= 2;
					if (nextSize < minSize)
						break;

					auto anyNonZero = false;
					for (const auto& v : std::span(readBuffer).subspan(nextSize, mipmapSize - nextSize))
						if ((anyNonZero = anyNonZero || v))
							break;
					if (anyNonZero)
						break;
				}
				maxMipmapSize = std::max(maxMipmapSize, mipmapSize);
			}
		} else {
			maxMipmapSize = mipmapSizes[i];
		}

		readBuffer.resize(BlockDataSize);
		for (uint32_t repeatI = 0; repeatI < repeatCount; repeatI++) {
			const auto blockAlignment = Align<uint32_t>(maxMipmapSize, BlockDataSize);

			SqData::TextureBlockHeaderLocator loc{
				.FirstBlockOffset = blockOffsetCounter,
				.TotalSize = 0,
				.DecompressedSize = maxMipmapSize,
				.FirstSubBlockIndex = blockLocators.empty() ? 0 : blockLocators.back().FirstSubBlockIndex + blockLocators.back().SubBlockCount,
				.SubBlockCount = blockAlignment.Count,
			};

			blockAlignment.IterateChunked([&](uint32_t, const uint32_t offset, const uint32_t length) {
				const auto sourceBuf = std::span(readBuffer).subspan(0, length);
				stream.ReadStream(offset, std::span(sourceBuf));

				if (deflater)
					deflater->Deflate(sourceBuf);
				const auto useCompressed = deflater && deflater->Result().size() < sourceBuf.size();
				const auto targetBuf = useCompressed ? deflater->Result() : sourceBuf;

				SqData::BlockHeader header{
					.HeaderSize = sizeof SqData::BlockHeader,
					.Version = 0,
					.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : SqData::BlockHeader::CompressedSizeNotCompressed,
					.DecompressedSize = length,
				};
				const auto alignmentInfo = Align(sizeof header + targetBuf.size());

				subBlockSizes.push_back(static_cast<uint16_t>(alignmentInfo.Alloc));
				blockOffsetCounter += subBlockSizes.back();
				loc.TotalSize += subBlockSizes.back();

				entryBody.resize(entryBody.size() + alignmentInfo.Alloc);
				auto ptr = entryBody.end() - static_cast<SSIZE_T>(alignmentInfo.Alloc);
				ptr = std::copy_n(reinterpret_cast<uint8_t*>(&header), sizeof header, ptr);
				ptr = std::copy(targetBuf.begin(), targetBuf.end(), ptr);
				std::fill_n(ptr, alignmentInfo.Pad, 0);
			}, mipmapOffsets[i] + mipmapSizes[i] * repeatI);

			blockLocators.emplace_back(loc);
		}
	}

	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(blockLocators.size());
	entryHeader.HeaderSize = static_cast<uint32_t>(Sqex::Align(
		sizeof SqData::FileEntryHeader +
		std::span(blockLocators).size_bytes() +
		std::span(subBlockSizes).size_bytes()));

	m_data.insert(m_data.end(),
		reinterpret_cast<char*>(&entryHeader),
		reinterpret_cast<char*>(&entryHeader + 1));
	m_data.insert(m_data.end(),
		reinterpret_cast<char*>(&blockLocators.front()),
		reinterpret_cast<char*>(&blockLocators.back() + 1));
	m_data.insert(m_data.end(),
		reinterpret_cast<char*>(&subBlockSizes.front()),
		reinterpret_cast<char*>(&subBlockSizes.back() + 1));
	m_data.resize(entryHeader.HeaderSize);
	m_data.insert(m_data.end(),
		texHeaderBytes.begin(),
		texHeaderBytes.end());
	m_data.insert(m_data.end(), entryBody.begin(), entryBody.end());

	m_data.resize(Align(m_data.size()));

	auto& fileEntryHeader = *reinterpret_cast<SqData::FileEntryHeader*>(&m_data[0]);
	reinterpret_cast<SqData::FileEntryHeader*>(&m_data[0])->SetSpaceUnits(m_data.size());
}

uint64_t Sqex::Sqpack::MemoryTextureEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	const auto available = static_cast<size_t>(std::min(length, m_data.size() - offset));
	if (!available)
		return 0;

	memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
	return available;
}

uint64_t Sqex::Sqpack::HotSwappableEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (offset >= m_reservedSize)
		return 0;
	if (offset + length > m_reservedSize)
		length = m_reservedSize - offset;

	auto target = std::span(static_cast<uint8_t*>(buf), static_cast<SSIZE_T>(length));
	const auto& underlyingStream = m_stream ? *m_stream : m_baseStream ? *m_baseStream : EmptyOrObfuscatedEntryProvider::Instance();
	const auto underlyingStreamLength = underlyingStream.StreamSize();
	const auto dataLength = offset < underlyingStreamLength ? std::min(length, underlyingStreamLength - offset) : 0;

	if (offset < underlyingStreamLength) {
		const auto dataTarget = target.subspan(0, static_cast<SSIZE_T>(dataLength));
		const auto readLength = static_cast<size_t>(underlyingStream.ReadStreamPartial(offset, &dataTarget[0], dataTarget.size_bytes()));
		if (readLength != dataTarget.size_bytes())
			throw std::logic_error(std::format("HotSwappableEntryProvider underlying data read fail: {}", underlyingStream.DescribeState()));
		target = target.subspan(readLength);
	}
	std::ranges::fill(target, 0);
	return length;
}

void Sqex::Sqpack::MemoryModelEntryProvider::Initialize(const RandomAccessStream& stream) {
	Model::Header header;
	stream.ReadStream(0, &header, sizeof header);

	SqData::FileEntryHeader entryHeader{
		.Type = SqData::FileEntryType::Model,
		.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
		.BlockCountOrVersion = header.Version,
	};
	SqData::ModelBlockLocator modelHeader{
		.VertexDeclarationCount = header.VertexDeclarationCount,
		.MaterialCount = header.MaterialCount,
		.LodCount = header.LodCount,
		.EnableIndexBufferStreaming = header.EnableIndexBufferStreaming,
		.EnableEdgeGeometry = header.EnableEdgeGeometry,
	};

	std::optional<ZlibReusableDeflater> deflater;
	if (m_compressionLevel)
		deflater.emplace(m_compressionLevel, Z_DEFLATED, -15);
	std::vector<uint8_t> entryBody;
	entryBody.reserve(static_cast<size_t>(stream.StreamSize()));

	std::vector<uint32_t> blockOffsets;
	std::vector<uint16_t> paddedBlockSizes;
	const auto getNextBlockOffset = [&]() {
		return paddedBlockSizes.empty() ? 0U : blockOffsets.back() + paddedBlockSizes.back();
	};

	std::vector<uint8_t> tempBuf(BlockDataSize);
	auto baseFileOffset = static_cast<uint32_t>(sizeof header);
	const auto generateSet = [&](uint32_t size) {
		const auto alignedDecompressedSize = Align(size).Alloc;
		const auto alignedBlock = Align<uint32_t, uint16_t>(size, BlockDataSize);
		const auto firstBlockOffset = size ? getNextBlockOffset() : 0;
		const auto firstBlockIndex = static_cast<uint16_t>(blockOffsets.size());
		alignedBlock.IterateChunked([&](auto, uint32_t offset, uint32_t size) {
			const auto sourceBuf = std::span(tempBuf).subspan(0, size);
			stream.ReadStream(offset, sourceBuf);
			if (deflater)
				deflater->Deflate(sourceBuf);
			const auto useCompressed = deflater && deflater->Result().size() < sourceBuf.size();
			const auto targetBuf = useCompressed ? deflater->Result() : sourceBuf;

			SqData::BlockHeader header{
				.HeaderSize = sizeof SqData::BlockHeader,
				.Version = 0,
				.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : SqData::BlockHeader::CompressedSizeNotCompressed,
				.DecompressedSize = static_cast<uint32_t>(sourceBuf.size()),
			};
			const auto alignmentInfo = Align(sizeof header + targetBuf.size());

			entryBody.resize(entryBody.size() + alignmentInfo.Alloc);

			auto ptr = entryBody.end() - static_cast<SSIZE_T>(alignmentInfo.Alloc);
			ptr = std::copy_n(reinterpret_cast<uint8_t*>(&header), sizeof header, ptr);
			ptr = std::copy(targetBuf.begin(), targetBuf.end(), ptr);
			std::fill_n(ptr, alignmentInfo.Pad, 0);

			blockOffsets.push_back(getNextBlockOffset());
			paddedBlockSizes.push_back(static_cast<uint32_t>(alignmentInfo.Alloc));
			}, baseFileOffset);
		const auto chunkSize = size ? getNextBlockOffset() - firstBlockOffset : 0;
		baseFileOffset += size;

		return std::make_tuple(
			alignedDecompressedSize,
			alignedBlock.Count,
			firstBlockOffset,
			firstBlockIndex,
			chunkSize
		);
	};

	std::tie(modelHeader.AlignedDecompressedSizes.Stack,
		modelHeader.BlockCount.Stack,
		modelHeader.FirstBlockOffsets.Stack,
		modelHeader.FirstBlockIndices.Stack,
		modelHeader.ChunkSizes.Stack) = generateSet(header.StackSize);

	std::tie(modelHeader.AlignedDecompressedSizes.Runtime,
		modelHeader.BlockCount.Runtime,
		modelHeader.FirstBlockOffsets.Runtime,
		modelHeader.FirstBlockIndices.Runtime,
		modelHeader.ChunkSizes.Runtime) = generateSet(header.RuntimeSize);

	for (size_t i = 0; i < 3; i++) {
		std::tie(modelHeader.AlignedDecompressedSizes.Vertex[i],
			modelHeader.BlockCount.Vertex[i],
			modelHeader.FirstBlockOffsets.Vertex[i],
			modelHeader.FirstBlockIndices.Vertex[i],
			modelHeader.ChunkSizes.Vertex[i]) = generateSet(header.VertexSize[i]);

		std::tie(modelHeader.AlignedDecompressedSizes.EdgeGeometryVertex[i],
			modelHeader.BlockCount.EdgeGeometryVertex[i],
			modelHeader.FirstBlockOffsets.EdgeGeometryVertex[i],
			modelHeader.FirstBlockIndices.EdgeGeometryVertex[i],
			modelHeader.ChunkSizes.EdgeGeometryVertex[i]) = generateSet(header.IndexOffset[i] ? header.IndexOffset[i] - baseFileOffset : 0);

		std::tie(modelHeader.AlignedDecompressedSizes.Index[i],
			modelHeader.BlockCount.Index[i],
			modelHeader.FirstBlockOffsets.Index[i],
			modelHeader.FirstBlockIndices.Index[i],
			modelHeader.ChunkSizes.Index[i]) = generateSet(header.IndexSize[i]);
	}

	entryHeader.HeaderSize = Align(static_cast<uint32_t>(sizeof entryHeader + sizeof modelHeader + std::span(paddedBlockSizes).size_bytes()));

	m_data.reserve(Align(entryHeader.HeaderSize + getNextBlockOffset()));
	m_data.insert(m_data.end(), reinterpret_cast<char*>(&entryHeader), reinterpret_cast<char*>(&entryHeader + 1));
	m_data.insert(m_data.end(), reinterpret_cast<char*>(&modelHeader), reinterpret_cast<char*>(&modelHeader + 1));
	if (!paddedBlockSizes.empty()) {
		m_data.insert(m_data.end(), reinterpret_cast<char*>(&paddedBlockSizes.front()), reinterpret_cast<char*>(&paddedBlockSizes.back() + 1));
		m_data.resize(entryHeader.HeaderSize, 0);
		m_data.insert(m_data.end(), entryBody.begin(), entryBody.end());
	} else
		m_data.resize(entryHeader.HeaderSize, 0);

	m_data.resize(Align(m_data.size()));
	reinterpret_cast<SqData::FileEntryHeader*>(&m_data[0])->SetSpaceUnits(m_data.size());
}

uint64_t Sqex::Sqpack::MemoryModelEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	const auto available = static_cast<size_t>(std::min(length, m_data.size() - offset));
	if (!available)
		return 0;

	memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
	return available;
}
