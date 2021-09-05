#include "pch.h"
#include "Sqex_Sqpack_EntryProvider.h"

#include "Sqex_Model.h"
#include "XaZlib.h"

uint64_t Sqex::Sqpack::EmptyEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (offset < sizeof SqData::FileEntryHeader) {
		const auto header = SqData::FileEntryHeader{
			.HeaderSize = sizeof SqData::FileEntryHeader,
			.Type = SqData::FileEntryType::Binary,
			.DecompressedSize = 0,
			.Unknown1 = 0,
			.BlockBufferSize = 0,
			.BlockCountOrVersion = 0,
		};
		const auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));
		const auto src = std::span(reinterpret_cast<const char*>(&header), header.HeaderSize)
			.subspan(static_cast<size_t>(offset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		return available;
	}

	return 0;
}

Sqex::Sqpack::LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider(EntryPathSpec spec, std::filesystem::path path, bool openImmediately)
	: EntryProvider(std::move(spec))
	, m_path(std::move(path))
	, m_initializationMutex(std::make_unique<std::mutex>())
	, m_stream(std::make_shared<FileRandomAccessStream>(m_path, 0, SIZE_MAX, openImmediately)) {
}

Sqex::Sqpack::LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider(EntryPathSpec spec, std::shared_ptr<RandomAccessStream> stream)
	: EntryProvider(std::move(spec))
	, m_path()
	, m_initializationMutex(std::make_unique<std::mutex>())
	, m_stream(std::move(stream)) {
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::StreamSize() const {
	const_cast<LazyFileOpeningEntryProvider*>(this)->Resolve();
	return StreamSize(*m_stream);
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	const_cast<LazyFileOpeningEntryProvider*>(this)->Resolve();
	return ReadStreamPartial(*m_stream, offset, buf, length);
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
	const auto rawSize = static_cast<uint32_t>(stream.StreamSize());
	const auto blockCount = (rawSize + BlockDataSize - 1) / BlockDataSize;
	m_header = {
		.HeaderSize = sizeof m_header + blockCount * sizeof SqData::BlockHeaderLocator,
		.Type = SqData::FileEntryType::Binary,
		.DecompressedSize = rawSize,
		.Unknown1 = BlockSize,
		.BlockBufferSize = BlockSize,
		.BlockCountOrVersion = blockCount,
	};
	const auto align = Align(m_header.HeaderSize);
	m_padBeforeData = align.Pad;
	m_header.HeaderSize = align;

	if (rawSize % BlockSize == 0) {
		m_size = m_header.HeaderSize + m_header.BlockCountOrVersion * BlockSize;
	} else {
		m_size = static_cast<uint32_t>(0
			+ m_header.HeaderSize
			+ (m_header.BlockCountOrVersion - 1) * BlockSize  // full blocks, up to the one before the last block
			+ Align(sizeof SqData::BlockHeader + m_header.DecompressedSize % BlockDataSize).Alloc  // the last block
			);
	}
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

			if (const auto padSize = Align(decompressedSize + sizeof SqData::BlockHeader).Pad;
				relativeOffset < padSize) {
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
		.Unknown1 = BlockSize,
		.BlockBufferSize = BlockSize,
		.BlockCountOrVersion = 0,
	};

	std::vector<std::vector<uint8_t>> blocks;
	std::vector<SqData::BlockHeaderLocator> locators;
	for (uint32_t i = 0; i < rawSize; i += BlockDataSize) {
		uint8_t buf[BlockDataSize];
		const auto len = std::min<uint32_t>(BlockDataSize, rawSize - i);
		stream.ReadStream(i, buf, len);
		auto compressed = Utils::ZlibCompress(buf, len, Z_BEST_COMPRESSION, Z_DEFLATED, -15);

		SqData::BlockHeader header{
			.HeaderSize = sizeof SqData::BlockHeader,
			.Version = 0,
			.CompressedSize = static_cast<uint32_t>(compressed.size()),
			.DecompressedSize = len,
		};
		const auto pad = Align(sizeof header + compressed.size()).Pad;

		locators.emplace_back(SqData::BlockHeaderLocator{
			locators.empty() ? 0 : locators.back().BlockSize + locators.back().Offset,
			static_cast<uint16_t>(sizeof SqData::BlockHeader + compressed.size() + pad),
			static_cast<uint16_t>(len)
			});

		blocks.emplace_back(std::vector<uint8_t>{ reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header + 1) });
		blocks.emplace_back(std::move(compressed));
		if (pad)
			blocks.emplace_back(std::vector<uint8_t>(pad, 0));
	}

	entryHeader.BlockCountOrVersion = static_cast<uint32_t>(locators.size());
	entryHeader.HeaderSize = static_cast<uint32_t>(Align(entryHeader.HeaderSize + std::span(locators).size_bytes()));
	m_data.insert(m_data.end(), reinterpret_cast<char*>(&entryHeader), reinterpret_cast<char*>(&entryHeader + 1));
	if (!locators.empty()) {
		m_data.insert(m_data.end(), reinterpret_cast<char*>(&locators.front()), reinterpret_cast<char*>(&locators.back() + 1));
		m_data.resize(entryHeader.HeaderSize, 0);
		for (const auto& block : blocks)
			m_data.insert(m_data.end(), block.begin(), block.end());
	} else
		m_data.resize(entryHeader.HeaderSize, 0);
}

uint64_t Sqex::Sqpack::MemoryBinaryEntryProvider::ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const {
	const auto available = static_cast<size_t>(std::min(length, m_data.size() - offset));
	if (!available)
		return 0;

	memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
	return available;
}

void Sqex::Sqpack::OnTheFlyModelEntryProvider::Initialize(const RandomAccessStream& stream) {

	Model::Header fileHeader;
	stream.ReadStream(0, &fileHeader, sizeof fileHeader);

	auto baseFileOffset = static_cast<uint32_t>(sizeof fileHeader);
	m_header.Entry.Type = SqData::FileEntryType::Model;
	m_header.Entry.DecompressedSize = static_cast<uint32_t>(stream.StreamSize());
	m_header.Entry.Unknown1 = 0;
	m_header.Entry.BlockBufferSize = BlockSize;
	m_header.Entry.BlockCountOrVersion = fileHeader.Version;

	m_header.Model.VertexDeclarationCount = fileHeader.VertexDeclarationCount;
	m_header.Model.MaterialCount = fileHeader.MaterialCount;
	m_header.Model.LodCount = fileHeader.LodCount;
	m_header.Model.EnableIndexBufferStreaming = fileHeader.EnableIndexBufferStreaming;
	m_header.Model.EnableEdgeGeometry = fileHeader.EnableEdgeGeometry;

	m_header.Model.AlignedDecompressedSizes.Stack = Align(fileHeader.StackSize);
	m_header.Model.FirstBlockOffsets.Stack = NextBlockOffset();
	m_header.Model.FirstBlockIndices.Stack = static_cast<uint16_t>(m_blockOffsets.size());
	m_header.Model.BlockCount.Stack = Align<uint32_t, uint16_t>(fileHeader.StackSize.Value(), BlockDataSize).Count;
	for (uint32_t i = 0; i < m_header.Model.BlockCount.Stack; ++i) {
		m_blockOffsets.push_back(NextBlockOffset());
		m_blockDataSizes.push_back(i == m_header.Model.BlockCount.Stack - 1 ? fileHeader.StackSize % BlockDataSize : BlockDataSize);
		m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
		m_actualFileOffsets.push_back(baseFileOffset + i * BlockDataSize);
	}
	m_header.Model.ChunkSizes.Stack = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Stack;
	baseFileOffset += fileHeader.StackSize;

	m_header.Model.AlignedDecompressedSizes.Runtime = Align(fileHeader.RuntimeSize);
	m_header.Model.FirstBlockOffsets.Runtime = NextBlockOffset();
	m_header.Model.FirstBlockIndices.Runtime = static_cast<uint16_t>(m_blockOffsets.size());
	m_header.Model.BlockCount.Runtime = Align<uint32_t, uint16_t>(fileHeader.RuntimeSize.Value(), BlockDataSize).Count;
	for (uint32_t i = 0; i < m_header.Model.BlockCount.Runtime; ++i) {
		m_blockOffsets.push_back(NextBlockOffset());
		m_blockDataSizes.push_back(i == m_header.Model.BlockCount.Runtime - 1 ? fileHeader.RuntimeSize % BlockDataSize : BlockDataSize);
		m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
		m_actualFileOffsets.push_back(baseFileOffset + i * BlockDataSize);
	}
	m_header.Model.ChunkSizes.Runtime = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Runtime;
	baseFileOffset += fileHeader.RuntimeSize;

	for (size_t i = 0; i < 3; i++) {
		m_header.Model.AlignedDecompressedSizes.Vertex[i] = Align(fileHeader.VertexSize[i]);
		m_header.Model.FirstBlockOffsets.Vertex[i] = NextBlockOffset();
		m_header.Model.FirstBlockIndices.Vertex[i] = static_cast<uint16_t>(m_blockOffsets.size());
		m_header.Model.BlockCount.Vertex[i] = Align<uint32_t, uint16_t>(fileHeader.VertexSize[i].Value(), BlockDataSize).Count;
		for (uint32_t j = 0; j < m_header.Model.BlockCount.Vertex[i]; ++j) {
			m_blockOffsets.push_back(NextBlockOffset());
			m_blockDataSizes.push_back(j == m_header.Model.BlockCount.Vertex[i] - 1 ? fileHeader.VertexSize[i] % BlockDataSize : BlockDataSize);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
			m_actualFileOffsets.push_back(baseFileOffset + j * BlockDataSize);
		}
		m_header.Model.ChunkSizes.Vertex[i] = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Vertex[i];
		baseFileOffset += fileHeader.VertexSize[i];

		const auto edgeGeometryVertexSize = fileHeader.IndexOffset[i] - baseFileOffset;
		m_header.Model.AlignedDecompressedSizes.EdgeGeometryVertex[i] = Align(edgeGeometryVertexSize);
		m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i] = NextBlockOffset();
		m_header.Model.FirstBlockIndices.EdgeGeometryVertex[i] = static_cast<uint16_t>(m_blockOffsets.size());
		m_header.Model.BlockCount.EdgeGeometryVertex[i] = Align<uint32_t, uint16_t>(edgeGeometryVertexSize, BlockDataSize).Count;
		for (uint32_t j = 0; j < m_header.Model.BlockCount.EdgeGeometryVertex[i]; ++j) {
			m_blockOffsets.push_back(NextBlockOffset());
			m_blockDataSizes.push_back(j == m_header.Model.BlockCount.EdgeGeometryVertex[i] - 1 ? edgeGeometryVertexSize % BlockDataSize : BlockDataSize);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
			m_actualFileOffsets.push_back(baseFileOffset + j * BlockDataSize);
		}
		m_header.Model.ChunkSizes.EdgeGeometryVertex[i] = NextBlockOffset() - m_header.Model.FirstBlockOffsets.EdgeGeometryVertex[i];
		baseFileOffset += edgeGeometryVertexSize;

		m_header.Model.AlignedDecompressedSizes.Index[i] = Align(fileHeader.IndexSize[i]);
		m_header.Model.FirstBlockOffsets.Index[i] = NextBlockOffset();
		m_header.Model.FirstBlockIndices.Index[i] = static_cast<uint16_t>(m_blockOffsets.size());
		m_header.Model.BlockCount.Index[i] = Align<uint32_t, uint16_t>(fileHeader.IndexSize[i].Value(), BlockDataSize).Count;
		for (uint32_t j = 0; j < m_header.Model.BlockCount.Index[i]; ++j) {
			m_blockOffsets.push_back(NextBlockOffset());
			m_blockDataSizes.push_back(j == m_header.Model.BlockCount.Index[i] - 1 ? fileHeader.IndexSize[i] % BlockDataSize : BlockDataSize);
			m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof SqData::BlockHeader + m_blockDataSizes.back())));
			m_actualFileOffsets.push_back(baseFileOffset + j * BlockDataSize);
		}
		m_header.Model.ChunkSizes.Index[i] = NextBlockOffset() - m_header.Model.FirstBlockOffsets.Index[i];
		baseFileOffset += fileHeader.IndexSize[i];
	}

	if (baseFileOffset > m_header.Entry.DecompressedSize)
		throw std::runtime_error("Bad model file (incomplete data)");

	m_header.Entry.HeaderSize = Align(static_cast<uint32_t>(sizeof ModelEntryHeader + std::span(m_blockDataSizes).size_bytes()));
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

	auto it = std::lower_bound(m_blockOffsets.begin(), m_blockOffsets.end(),
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

	if (const auto endPadding = AlignEntry().Pad;
		relativeOffset < endPadding) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(endPadding - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
	}

	return length - out.size_bytes();
}

void Sqex::Sqpack::OnTheFlyTextureEntryProvider::Initialize(const RandomAccessStream& stream) {
	auto entryHeader = SqData::FileEntryHeader{
		.HeaderSize = sizeof SqData::FileEntryHeader,
		.Type = SqData::FileEntryType::Texture,
		.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
		.Unknown1 = 0,
		.BlockBufferSize = BlockSize,
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
			blockOffsetCounter,
			(subBlockCount * sizeof SqData::BlockHeader + mipmapSize),
			mipmapSize,
			i == 0 ? 0 : m_blockLocators.back().FirstSubBlockIndex + m_blockLocators.back().SubBlockCount,
			subBlockCount,
		};
		for (size_t j = 0; j < subBlockCount; ++j) {
			m_subBlockSizes.push_back(
				sizeof SqData::BlockHeader +
				(j == subBlockCount - 1 ? mipmapSize % BlockDataSize : BlockDataSize)
			);
			blockOffsetCounter += sizeof SqData::BlockHeader + m_subBlockSizes.back();
		}

		m_blockLocators.emplace_back(loc);
	}
	for (auto& loc : m_blockLocators) {
		loc.FirstBlockOffset = loc.FirstBlockOffset + static_cast<uint32_t>(std::span(m_texHeaderBytes).size_bytes());
	}

	entryHeader.HeaderSize = entryHeader.HeaderSize + static_cast<uint32_t>(
		std::span(m_blockLocators).size_bytes() +
		std::span(m_subBlockSizes).size_bytes());

	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&entryHeader),
		reinterpret_cast<char*>(&entryHeader + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_blockLocators.front()),
		reinterpret_cast<char*>(&m_blockLocators.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		reinterpret_cast<char*>(&m_subBlockSizes.front()),
		reinterpret_cast<char*>(&m_subBlockSizes.back() + 1));
	m_mergedHeader.insert(m_mergedHeader.end(),
		m_texHeaderBytes.begin(),
		m_texHeaderBytes.end());

	m_size += m_mergedHeader.size();
	for (const auto mipmapSize : m_mipmapSizes) {
		m_size += (mipmapSize + BlockDataSize - 1) / BlockDataSize * sizeof SqData::BlockHeader + mipmapSize;
	}
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

	if (relativeOffset < m_size) {
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
			}
		}
	}

	return length - out.size_bytes();
}
