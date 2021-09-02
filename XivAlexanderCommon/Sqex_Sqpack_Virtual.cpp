#include "pch.h"
#include "Sqex_Sqpack_Virtual.h"
#include "Sqex_Sqpack_Reader.h"
#include "Utils__Zlib.h"

uint32_t Sqex::Sqpack::VirtualSqPack::EmptyEntryProvider::StreamSize() const {
	return sizeof SqData::FileEntryHeader;
}

size_t Sqex::Sqpack::VirtualSqPack::EmptyEntryProvider::ReadStreamPartial(const uint64_t offset, void* const buf, const size_t length) const {
	if (offset < sizeof SqData::FileEntryHeader) {
		const auto header = SqData::FileEntryHeader{
			.HeaderSize = sizeof SqData::FileEntryHeader,
			.Type = SqData::FileEntryType::Binary,
			.DecompressedSize = 0,
			.Unknown1 = 0,
			.BlockBufferSize = 0,
			.BlockCountOrVersion = 0,
		};
		const auto out = std::span(static_cast<char*>(buf), length);
		const auto src = std::span(reinterpret_cast<const char*>(&header), header.HeaderSize)
			.subspan(static_cast<size_t>(offset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		return available;
	}

	return 0;
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::VirtualSqPack::EmptyEntryProvider::EntryType() const {
	return SqData::FileEntryType::Empty;
}

Sqex::Sqpack::VirtualSqPack::OnTheFlyBinaryEntryProvider::OnTheFlyBinaryEntryProvider(std::filesystem::path path)
	: m_path(std::move(path)) {
	const auto rawSize = static_cast<uint32_t>(file_size(m_path));
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
}

Sqex::Sqpack::VirtualSqPack::OnTheFlyBinaryEntryProvider::~OnTheFlyBinaryEntryProvider() = default;

uint32_t Sqex::Sqpack::VirtualSqPack::OnTheFlyBinaryEntryProvider::StreamSize() const {
	if (!m_header.BlockCountOrVersion)
		return m_header.HeaderSize;

	return m_header.HeaderSize +
		m_header.DecompressedSize +
		(m_header.BlockCountOrVersion - 1) * BlockPadSize +  // full blocks, up to the one before the last block
		Align(m_header.DecompressedSize % BlockDataSize).Pad;  // the last block
}

size_t Sqex::Sqpack::VirtualSqPack::OnTheFlyBinaryEntryProvider::ReadStreamPartial(const uint64_t offset, void* const buf, const size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
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

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= sizeof locator;
		}
	} else
		relativeOffset -= m_header.BlockCountOrVersion * sizeof SqData::BlockHeaderLocator;

	if (relativeOffset < m_padBeforeData) {
		const auto available = std::min(out.size_bytes(), static_cast<size_t>(m_padBeforeData - relativeOffset));
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
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

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= sizeof SqData::BlockHeader;

			if (relativeOffset < decompressedSize) {
				if (!m_hFile)
					m_hFile = Utils::Win32::File::Create(m_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);

				const auto available = std::min(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
				m_hFile.Read(i * BlockDataSize + relativeOffset, &out[0], available);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= decompressedSize;

			if (const auto padSize = Align(decompressedSize + sizeof SqData::BlockHeader).Pad;
				relativeOffset < padSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(static_cast<size_t>(available));
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
			} else
				relativeOffset -= padSize;
		}
	}

	return length - out.size_bytes();
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::VirtualSqPack::OnTheFlyBinaryEntryProvider::EntryType() const {
	return SqData::FileEntryType::Binary;
}

Sqex::Sqpack::VirtualSqPack::MemoryBinaryEntryProvider::MemoryBinaryEntryProvider(const std::filesystem::path & path) {
	const auto file = Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0);

	const auto rawSize = static_cast<uint32_t>(file.Length());
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
		file.Read(i, buf, len);
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

Sqex::Sqpack::VirtualSqPack::MemoryBinaryEntryProvider::~MemoryBinaryEntryProvider() = default;

uint32_t Sqex::Sqpack::VirtualSqPack::MemoryBinaryEntryProvider::StreamSize() const {
	return static_cast<uint32_t>(m_data.size());
}

size_t Sqex::Sqpack::VirtualSqPack::MemoryBinaryEntryProvider::ReadStreamPartial(const uint64_t offset, void* const buf, const size_t length) const {
	const auto available = std::min(length, static_cast<size_t>(m_data.size() - offset));
	if (!available)
		return 0;

	memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
	return available;
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::VirtualSqPack::MemoryBinaryEntryProvider::EntryType() const {
	return SqData::FileEntryType::Binary;
}

Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::OnTheFlyModelEntryProvider(const std::filesystem::path & path)
	: m_hFile(Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)) {
	const auto fileHeader = m_hFile.Read< SqData::ModelHeader>(0);
	auto baseFileOffset = static_cast<uint32_t>(sizeof fileHeader);
	m_header.Entry.Type = SqData::FileEntryType::Model;
	m_header.Entry.DecompressedSize = static_cast<uint32_t>(m_hFile.Length());
	m_header.Entry.Unknown1 = 0;
	m_header.Entry.BlockBufferSize = BlockSize;
	m_header.Entry.BlockCountOrVersion = fileHeader.Version;

	m_header.Model.VertexDeclarationCount = fileHeader.VertexDeclarationCount;
	m_header.Model.MaterialCount = fileHeader.MaterialCount;
	m_header.Model.LodCount = fileHeader.LodCount;
	m_header.Model.EnableIndexBufferStreaming = fileHeader.EnableIndexBufferStreaming;
	m_header.Model.EnableEdgeGeometry = fileHeader.EnableEdgeGeometry;

	m_header.Model.DecompressedSizes.Stack = Align(fileHeader.StackSize);
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

	m_header.Model.DecompressedSizes.Runtime = Align(fileHeader.RuntimeSize);
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
		m_header.Model.DecompressedSizes.Vertex[i] = Align(fileHeader.VertexSize[i]);
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
		m_header.Model.DecompressedSizes.EdgeGeometryVertex[i] = Align(edgeGeometryVertexSize);
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

		m_header.Model.DecompressedSizes.Index[i] = Align(fileHeader.IndexSize[i]);
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

Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::~OnTheFlyModelEntryProvider() = default;

Sqex::Sqpack::AlignResult<unsigned> Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::AlignEntry() const {
	return Align(m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back()));
}

uint32_t Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::NextBlockOffset() const {
	return m_paddedBlockSizes.empty() ? 0U : Align(m_blockOffsets.back() + m_paddedBlockSizes.back());
}

uint32_t Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::StreamSize() const {
	return AlignEntry();
}

size_t Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
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

		if (out.empty())
			return length - out.size_bytes();
	} else
		relativeOffset -= srcTyped.size_bytes();

	if (const auto padBeforeBlocks = Align(sizeof ModelEntryHeader + std::span(m_paddedBlockSizes).size_bytes()).Pad;
		relativeOffset < padBeforeBlocks) {
		const auto available = std::min(out.size_bytes(), padBeforeBlocks - relativeOffset);
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
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
			const auto available = std::min(out.size_bytes(), *it - relativeOffset);
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
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

			if (out.empty())
				return length - out.size_bytes();
		} else
			relativeOffset -= sizeof SqData::BlockHeader;

		if (relativeOffset < m_blockDataSizes[i]) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(m_blockDataSizes[i] - relativeOffset));
			m_hFile.Read(m_actualFileOffsets[i] + relativeOffset, &out[0], available);
			out = out.subspan(available);
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
		} else
			relativeOffset -= m_blockDataSizes[i];

		if (const auto padSize = m_paddedBlockSizes[i] - m_blockDataSizes[i] - sizeof SqData::BlockHeader;
			relativeOffset < padSize) {
			const auto available = std::min(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
			std::fill_n(out.begin(), available, 0);
			out = out.subspan(static_cast<size_t>(available));
			relativeOffset = 0;

			if (out.empty())
				return length - out.size_bytes();
		} else
			relativeOffset -= padSize;
	}

	if (const auto endPadding = AlignEntry().Pad;
		relativeOffset < endPadding) {
		const auto available = std::min(out.size_bytes(), endPadding - relativeOffset);
		std::fill_n(out.begin(), available, 0);
		out = out.subspan(available);
	}

	return length - out.size_bytes();
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::VirtualSqPack::OnTheFlyModelEntryProvider::EntryType() const {
	return SqData::FileEntryType::Model;
}

const Sqex::Sqpack::SqData::TexHeader& Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::AsTexHeader() const {
	return *reinterpret_cast<const SqData::TexHeader*>(&m_texHeaderBytes[0]);
}

std::span<const uint32_t> Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::AsMipmapOffsets() const {
	return std::span(
		reinterpret_cast<const uint32_t*>(&m_texHeaderBytes[sizeof SqData::TexHeader]),
		AsTexHeader().MipmapCount
	);
}

Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::OnTheFlyTextureEntryProvider(std::filesystem::path path)
	: m_hFile(Utils::Win32::File::Create(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0)) {

	auto entryHeader = SqData::FileEntryHeader{
		.HeaderSize = sizeof SqData::FileEntryHeader,
		.Type = SqData::FileEntryType::Texture,
		.DecompressedSize = static_cast<uint32_t>(m_hFile.Length()),
		.Unknown1 = 0,
		.BlockBufferSize = BlockSize,
	};

	m_texHeaderBytes.resize(sizeof SqData::TexHeader);
	m_hFile.Read(0, std::span(m_texHeaderBytes));
	entryHeader.BlockCountOrVersion = AsTexHeader().MipmapCount;

	m_texHeaderBytes.resize(sizeof SqData::TexHeader + AsTexHeader().MipmapCount * sizeof uint32_t);
	m_hFile.Read(sizeof SqData::TexHeader, std::span(
		reinterpret_cast<uint32_t*>(&m_texHeaderBytes[sizeof SqData::TexHeader]),
		AsTexHeader().MipmapCount.Value()
	));

	m_texHeaderBytes.resize(AsMipmapOffsets()[0]);
	m_hFile.Read(0, std::span(m_texHeaderBytes).subspan(0, AsMipmapOffsets()[0]));

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

Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::~OnTheFlyTextureEntryProvider() = default;

uint32_t Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::StreamSize() const {
	return static_cast<uint32_t>(m_size);
}

size_t Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::ReadStreamPartial(const uint64_t offset, void* const buf, const size_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	if (relativeOffset < m_mergedHeader.size()) {
		const auto src = std::span(m_mergedHeader)
			.subspan(static_cast<size_t>(relativeOffset));
		const auto available = std::min(out.size_bytes(), src.size_bytes());
		std::copy_n(src.begin(), available, out.begin());
		out = out.subspan(available);
		relativeOffset = 0;

		if (out.empty())
			return length - out.size_bytes();
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
				const auto available = std::min(out.size_bytes(), it->FirstBlockOffset - relativeOffset);
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					return length - out.size_bytes();
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

					if (out.empty())
						return length - out.size_bytes();
				} else
					relativeOffset -= sizeof SqData::BlockHeader;

				if (relativeOffset < decompressedSize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
					m_hFile.Read(AsMipmapOffsets()[blockIndex] + j * BlockDataSize + relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty())
						return length - out.size_bytes();
				} else
					relativeOffset -= decompressedSize;
			}
		}
	}

	return length - out.size_bytes();
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::VirtualSqPack::OnTheFlyTextureEntryProvider::EntryType() const {
	return SqData::FileEntryType::Texture;
}

Sqex::Sqpack::VirtualSqPack::PartialFileViewEntryProvider::PartialFileViewEntryProvider(Utils::Win32::File file, uint64_t offset, uint32_t length)
	: m_file(std::move(file))
	, m_offset(offset)
	, m_size(length) {
}

Sqex::Sqpack::VirtualSqPack::PartialFileViewEntryProvider::~PartialFileViewEntryProvider() = default;

uint32_t Sqex::Sqpack::VirtualSqPack::PartialFileViewEntryProvider::StreamSize() const {
	return m_size;
}

size_t Sqex::Sqpack::VirtualSqPack::PartialFileViewEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, size_t length) const {
	if (offset >= m_size)
		return 0;

	return m_file.Read(m_offset + offset, buf, std::min(length, static_cast<size_t>(m_size - offset)));
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::VirtualSqPack::PartialFileViewEntryProvider::EntryType() const {
	if (!m_entryTypeFetched) {
		m_entryType = m_file.Read<SqData::FileEntryHeader>(m_offset).Type;
		m_entryTypeFetched = true;
	}
	return m_entryType;
}

Sqex::Sqpack::VirtualSqPack::VirtualSqPack() = default;

Sqex::Sqpack::VirtualSqPack::AddEntryResult& Sqex::Sqpack::VirtualSqPack::AddEntryResult::operator+=(const AddEntryResult & r) {
	AddedCount += r.AddedCount;
	ReplacedCount += r.ReplacedCount;
	return *this;
}

Sqex::Sqpack::VirtualSqPack::AddEntryResult Sqex::Sqpack::VirtualSqPack::AddEntry(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, std::unique_ptr<EntryProvider> provider, bool overwriteExisting) {
	if (m_frozen)
		throw std::runtime_error("Trying to operate on a frozen VirtualSqPack");

	if (PathHash != Entry::NoEntryHash || NameHash != Entry::NoEntryHash) {
		const auto it = m_pathNameTupleEntryPointerMap.find(std::make_pair(PathHash, NameHash));
		if (it != m_pathNameTupleEntryPointerMap.end()) {
			if (!overwriteExisting)
				return { 0, 0, 1 };
			it->second->Provider = std::move(provider);
			if (FullPathHash != Entry::NoEntryHash) {
				m_fullPathEntryPointerMap.erase(it->second->FullPathHash);
				it->second->FullPathHash = FullPathHash;
				m_fullPathEntryPointerMap.insert_or_assign(FullPathHash, it->second);
				return { 0, 1 };
			}
		}
	}
	if (FullPathHash != Entry::NoEntryHash) {
		const auto it = m_fullPathEntryPointerMap.find(FullPathHash);
		if (it != m_fullPathEntryPointerMap.end()) {
			if (!overwriteExisting)
				return { 0, 0, 1 };
			it->second->Provider = std::move(provider);
			if (PathHash != Entry::NoEntryHash || NameHash != Entry::NoEntryHash) {
				m_pathNameTupleEntryPointerMap.erase(std::make_pair(it->second->PathHash, it->second->NameHash));
				it->second->PathHash = PathHash;
				it->second->NameHash = NameHash;
				m_pathNameTupleEntryPointerMap.insert_or_assign(std::make_pair(PathHash, NameHash), it->second);
				return { 0, 1 };
			}
		}
	}

	auto entry = std::make_unique<Entry>(PathHash, NameHash, FullPathHash, 0, 0, 0, 0, SqIndex::LEDataLocator{ 0, 0 }, std::move(provider));
	const auto ptr = entry.get();
	m_entries.emplace_back(std::move(entry));
	if (FullPathHash != Entry::NoEntryHash)
		m_fullPathEntryPointerMap.insert_or_assign(FullPathHash, ptr);
	if (PathHash != Entry::NoEntryHash || NameHash != Entry::NoEntryHash)
		m_pathNameTupleEntryPointerMap.insert_or_assign(std::make_pair(PathHash, NameHash), ptr);
	return { 1 };
}

Sqex::Sqpack::VirtualSqPack::AddEntryResult Sqex::Sqpack::VirtualSqPack::AddEntriesFromSqPack(const std::filesystem::path & indexPath, bool overwriteExisting, bool overwriteUnknownSegments) {
	if (m_frozen)
		throw std::runtime_error("Trying to operate on a frozen VirtualSqPack");

	FileSystemSqPack m_original{ indexPath, false };

	AddEntryResult result{};

	if (overwriteUnknownSegments) {
		m_sqpackIndexSegment2 = std::move(m_original.Index.DataFileSegment);
		m_sqpackIndexSegment3 = std::move(m_original.Index.Segment3);
		m_sqpackIndex2Segment2 = std::move(m_original.Index2.DataFileSegment);
		m_sqpackIndex2Segment3 = std::move(m_original.Index2.Segment3);
	}

	std::vector<size_t> openFileIndexMap;
	for (auto& f : m_original.Data) {
		const auto curItemPath = f.FileOnDisk.ResolveName();
		size_t found;
		for (found = 0; found < m_openFiles.size(); ++found) {
			if (equivalent(m_openFiles[found].ResolveName(), curItemPath)) {
				break;
			}
		}
		if (found == m_openFiles.size()) {
			m_openFiles.emplace_back(std::move(f.FileOnDisk));
		}
		openFileIndexMap.push_back(found);
	}

	for (const auto& entry : m_original.Files) {
		result += AddEntry(entry.IndexEntry.PathHash, entry.IndexEntry.NameHash, entry.Index2Entry.FullPathHash,
			std::make_unique<PartialFileViewEntryProvider>(
				Utils::Win32::File{ m_openFiles[openFileIndexMap[entry.DataFileIndex]], false },
				entry.DataEntryOffset, entry.DataEntrySize),
			overwriteExisting);
	}

	return result;
}

Sqex::Sqpack::VirtualSqPack::AddEntryResult Sqex::Sqpack::VirtualSqPack::AddEntryFromFile(uint32_t PathHash, uint32_t NameHash, uint32_t FullPathHash, const std::filesystem::path & path, bool overwriteExisting) {
	if (m_frozen)
		throw std::runtime_error("Trying to operate on a frozen VirtualSqPack");

	if (file_size(path) == 0) {
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<EmptyEntryProvider>(), overwriteExisting);
	} else if (path.extension() == ".tex") {
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<OnTheFlyTextureEntryProvider>(path), overwriteExisting);
	} else if (path.extension() == ".mdl") {
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<OnTheFlyModelEntryProvider>(path), overwriteExisting);
	} else {
		// return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<MemoryBinaryEntryProvider>(path));
		return AddEntry(PathHash, NameHash, FullPathHash, std::make_unique<OnTheFlyBinaryEntryProvider>(path), overwriteExisting);
	}
}

size_t Sqex::Sqpack::VirtualSqPack::NumOfDataFiles() const {
	return m_sqpackDataSubHeaders.size();
}

Sqex::Sqpack::SqData::Header& Sqex::Sqpack::VirtualSqPack::AllocateDataSpace(size_t length, bool strict) {
	if (m_sqpackDataSubHeaders.empty() ||
		sizeof SqpackHeader + sizeof SqData::Header + m_sqpackDataSubHeaders.back().DataSize + length > m_sqpackDataSubHeaders.back().MaxFileSize) {
		if (strict && !m_sqpackDataSubHeaders.empty())
			m_sqpackDataSubHeaders.back().Sha1.SetFromSpan(&m_sqpackDataSubHeaders.back(), 1);
		m_sqpackDataSubHeaders.emplace_back(SqData::Header{
			.HeaderSize = sizeof SqData::Header,
			.Unknown1 = SqData::Header::Unknown1_Value,
			.DataSize = 0,
			.SpanIndex = static_cast<uint32_t>(m_sqpackDataSubHeaders.size()),
			.MaxFileSize = SqData::Header::MaxFileSize_MaxValue,
			});
	}
	return m_sqpackDataSubHeaders.back();
}

void Sqex::Sqpack::VirtualSqPack::Freeze(bool strict) {
	if (m_frozen)
		throw std::runtime_error("Cannot freeze again");

	m_fileEntries1.clear();
	m_fileEntries2.clear();

	m_sqpackIndexSubHeader.DataFilesSegment.Count = 1;
	m_sqpackIndex2SubHeader.DataFilesSegment.Count = 1;

	for (const auto& entry : m_entries) {
		entry->BlockSize = entry->Provider->StreamSize();
		entry->PadSize = Align(entry->BlockSize).Pad;

		auto& dataSubHeader = AllocateDataSpace(0ULL + entry->BlockSize + entry->PadSize, strict);
		entry->DataFileIndex = static_cast<uint32_t>(m_sqpackDataSubHeaders.size() - 1);
		entry->OffsetAfterHeaders = dataSubHeader.DataSize;

		dataSubHeader.DataSize = dataSubHeader.DataSize + entry->BlockSize + entry->PadSize;

		const auto dataLocator = SqIndex::LEDataLocator{
			entry->DataFileIndex,
			sizeof SqpackHeader + sizeof SqData::Header + entry->OffsetAfterHeaders,
		};
		entry->Locator = dataLocator;
		m_fileEntries1.emplace_back(SqIndex::FileSegmentEntry{ entry->NameHash, entry->PathHash, dataLocator, 0 });
		m_fileEntries2.emplace_back(SqIndex::FileSegmentEntry2{ entry->FullPathHash, dataLocator });
	}

	std::sort(m_fileEntries1.begin(), m_fileEntries1.end(), [](const SqIndex::FileSegmentEntry& l, const SqIndex::FileSegmentEntry& r) {
		if (l.PathHash == r.PathHash)
			return l.NameHash < r.NameHash;
		else
			return l.PathHash < r.PathHash;
	});
	std::sort(m_fileEntries2.begin(), m_fileEntries2.end(), [](const SqIndex::FileSegmentEntry2& l, const SqIndex::FileSegmentEntry2& r) {
		return l.FullPathHash < r.FullPathHash;
	});

	memcpy(m_sqpackIndexHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_sqpackIndexHeader.HeaderSize = sizeof SqpackHeader;
	m_sqpackIndexHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	m_sqpackIndexHeader.Type = SqpackType::SqIndex;
	m_sqpackIndexHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		m_sqpackIndexHeader.Sha1.SetFromSpan(&m_sqpackIndexHeader, 1);

	m_sqpackIndexSubHeader.HeaderSize = sizeof SqIndex::Header;
	m_sqpackIndexSubHeader.Type = SqIndex::Header::IndexType::Index;
	m_sqpackIndexSubHeader.FileSegment.Count = 1;
	m_sqpackIndexSubHeader.FileSegment.Offset = m_sqpackIndexHeader.HeaderSize + m_sqpackIndexSubHeader.HeaderSize;
	m_sqpackIndexSubHeader.FileSegment.Size = static_cast<uint32_t>(std::span(m_fileEntries1).size_bytes());
	m_sqpackIndexSubHeader.DataFilesSegment.Count = static_cast<uint32_t>(m_sqpackDataSubHeaders.size());
	m_sqpackIndexSubHeader.DataFilesSegment.Offset = m_sqpackIndexSubHeader.FileSegment.Offset + m_sqpackIndexSubHeader.FileSegment.Size;
	m_sqpackIndexSubHeader.DataFilesSegment.Size = static_cast<uint32_t>(std::span(m_sqpackIndexSegment2).size_bytes());
	m_sqpackIndexSubHeader.UnknownSegment3.Count = 0;
	m_sqpackIndexSubHeader.UnknownSegment3.Offset = m_sqpackIndexSubHeader.DataFilesSegment.Offset + m_sqpackIndexSubHeader.DataFilesSegment.Size;
	m_sqpackIndexSubHeader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(m_sqpackIndexSegment3).size_bytes());
	m_sqpackIndexSubHeader.FolderSegment.Count = 0;
	m_sqpackIndexSubHeader.FolderSegment.Offset = m_sqpackIndexSubHeader.UnknownSegment3.Offset + m_sqpackIndexSubHeader.UnknownSegment3.Size;
	for (size_t i = 0; i < m_fileEntries1.size(); ++i) {
		const auto& entry = m_fileEntries1[i];
		if (m_folderEntries.empty() || m_folderEntries.back().NameHash != entry.PathHash) {
			m_folderEntries.emplace_back(
				entry.PathHash,
				static_cast<uint32_t>(m_sqpackIndexSubHeader.FileSegment.Offset + i * sizeof entry),
				static_cast<uint32_t>(sizeof entry),
				0);
		} else {
			m_folderEntries.back().FileSegmentSize = m_folderEntries.back().FileSegmentSize + sizeof entry;
		}
	}
	m_sqpackIndexSubHeader.FolderSegment.Size = static_cast<uint32_t>(std::span(m_folderEntries).size_bytes());
	if (strict)
		m_sqpackIndexSubHeader.Sha1.SetFromSpan(&m_sqpackIndexSubHeader, 1);

	memcpy(m_sqpackIndex2Header.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_sqpackIndex2Header.HeaderSize = sizeof SqpackHeader;
	m_sqpackIndex2Header.Unknown1 = SqpackHeader::Unknown1_Value;
	m_sqpackIndex2Header.Type = SqpackType::SqIndex;
	m_sqpackIndex2Header.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		m_sqpackIndex2Header.Sha1.SetFromSpan(&m_sqpackIndex2Header, 1);

	m_sqpackIndex2SubHeader.HeaderSize = sizeof SqIndex::Header;
	m_sqpackIndex2SubHeader.Type = SqIndex::Header::IndexType::Index2;
	m_sqpackIndex2SubHeader.FileSegment.Count = 1;
	m_sqpackIndex2SubHeader.FileSegment.Offset = m_sqpackIndex2Header.HeaderSize + m_sqpackIndex2SubHeader.HeaderSize;
	m_sqpackIndex2SubHeader.FileSegment.Size = static_cast<uint32_t>(std::span(m_fileEntries2).size_bytes());
	m_sqpackIndex2SubHeader.DataFilesSegment.Count = static_cast<uint32_t>(m_sqpackDataSubHeaders.size());
	m_sqpackIndex2SubHeader.DataFilesSegment.Offset = m_sqpackIndex2SubHeader.FileSegment.Offset + m_sqpackIndex2SubHeader.FileSegment.Size;
	m_sqpackIndex2SubHeader.DataFilesSegment.Size = static_cast<uint32_t>(std::span(m_sqpackIndex2Segment2).size_bytes());
	m_sqpackIndex2SubHeader.UnknownSegment3.Count = 0;
	m_sqpackIndex2SubHeader.UnknownSegment3.Offset = m_sqpackIndex2SubHeader.DataFilesSegment.Offset + m_sqpackIndex2SubHeader.DataFilesSegment.Size;
	m_sqpackIndex2SubHeader.UnknownSegment3.Size = static_cast<uint32_t>(std::span(m_sqpackIndex2Segment3).size_bytes());
	m_sqpackIndex2SubHeader.FolderSegment.Count = 0;
	m_sqpackIndex2SubHeader.FolderSegment.Offset = m_sqpackIndex2SubHeader.UnknownSegment3.Offset + m_sqpackIndex2SubHeader.UnknownSegment3.Size;
	m_sqpackIndex2SubHeader.FolderSegment.Size = 0;
	if (strict)
		m_sqpackIndex2SubHeader.Sha1.SetFromSpan(&m_sqpackIndex2SubHeader, 1);

	memcpy(m_sqpackDataHeader.Signature, SqpackHeader::Signature_Value, sizeof SqpackHeader::Signature_Value);
	m_sqpackDataHeader.HeaderSize = sizeof SqpackHeader;
	m_sqpackDataHeader.Unknown1 = SqpackHeader::Unknown1_Value;
	m_sqpackDataHeader.Type = SqpackType::SqData;
	m_sqpackDataHeader.Unknown2 = SqpackHeader::Unknown2_Value;
	if (strict)
		m_sqpackDataHeader.Sha1.SetFromSpan(&m_sqpackDataHeader, 1);

	m_frozen = true;
}

size_t Sqex::Sqpack::VirtualSqPack::ReadIndex1(const uint64_t offset, void* const buf, const size_t length) const {
	if (!m_frozen)
		throw std::runtime_error("Trying to operate on a non frozen VirtualSqPack");
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	for (const auto& [ptr, cb] : std::initializer_list<std::tuple<const void*, size_t>>{
		{&m_sqpackIndexHeader, sizeof m_sqpackIndexHeader},
		{&m_sqpackIndexSubHeader, sizeof m_sqpackIndexSubHeader},
		{m_fileEntries1.data(), std::span(m_fileEntries1).size_bytes()},
		{m_sqpackIndexSegment2.data(), std::span(m_sqpackIndexSegment2).size_bytes()},
		{m_sqpackIndexSegment3.data(), std::span(m_sqpackIndexSegment3).size_bytes()},
		{m_folderEntries.data(), std::span(m_folderEntries).size_bytes()},
		}) {
		if (relativeOffset < cb) {
			const auto src = std::span(static_cast<const char*>(ptr), cb)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= cb;

		if (out.empty())
			return length - out.size_bytes();
	}

	return length - out.size_bytes();
}

size_t Sqex::Sqpack::VirtualSqPack::ReadIndex2(const uint64_t offset, void* const buf, const size_t length) const {
	if (!m_frozen)
		throw std::runtime_error("Trying to operate on a non frozen VirtualSqPack");
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	for (const auto& [ptr, cb] : std::initializer_list<std::tuple<const void*, size_t>>{
		{&m_sqpackIndex2Header, sizeof m_sqpackIndex2Header},
		{&m_sqpackIndex2SubHeader, sizeof m_sqpackIndex2SubHeader},
		{m_fileEntries2.data(), std::span(m_fileEntries2).size_bytes()},
		{m_sqpackIndex2Segment2.data(), std::span(m_sqpackIndex2Segment2).size_bytes()},
		{m_sqpackIndex2Segment3.data(), std::span(m_sqpackIndex2Segment3).size_bytes()},
		}) {
		if (relativeOffset < cb) {
			const auto src = std::span(static_cast<const char*>(ptr), cb)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= cb;

		if (out.empty())
			return length - out.size_bytes();
	}

	return length - out.size_bytes();
}

size_t Sqex::Sqpack::VirtualSqPack::ReadData(uint32_t datIndex, const uint64_t offset, void* const buf, const size_t length) const {
	if (!m_frozen)
		throw std::runtime_error("Trying to operate on a non frozen VirtualSqPack");
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), length);

	for (const auto& [ptr, cb] : std::initializer_list<std::tuple<const void*, size_t>>{
		{&m_sqpackDataHeader, sizeof m_sqpackDataHeader},
		{&m_sqpackDataSubHeaders[datIndex], sizeof m_sqpackDataSubHeaders[datIndex]},
		}) {
		if (relativeOffset < cb) {
			const auto src = std::span(static_cast<const char*>(ptr), cb)
				.subspan(static_cast<size_t>(relativeOffset));
			const auto available = std::min(out.size_bytes(), src.size_bytes());
			std::copy_n(src.begin(), available, out.begin());
			out = out.subspan(available);
			relativeOffset = 0;
		} else
			relativeOffset -= cb;

		if (out.empty())
			return length - out.size_bytes();
	}

	auto it = std::lower_bound(m_entries.begin(), m_entries.end(), nullptr, [&](const std::unique_ptr<Entry>& l, const std::unique_ptr<Entry>& r) {
		const auto ldfi = l ? l->DataFileIndex : datIndex;
		const auto rdfi = r ? r->DataFileIndex : datIndex;
		if (ldfi == rdfi) {
			const auto lo = l ? l->OffsetAfterHeaders : relativeOffset;
			const auto ro = r ? r->OffsetAfterHeaders : relativeOffset;
			return lo < ro;
		} else
			return ldfi < rdfi;
	});
	if (it != m_entries.begin())
		--it;
	if (it != m_entries.end()) {
		relativeOffset -= it->get()->OffsetAfterHeaders;
		if (relativeOffset >= INT32_MAX)
			__debugbreak();

		for (; it < m_entries.end(); ++it) {
			const auto& entry = *it->get();

			if (relativeOffset < entry.BlockSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.BlockSize - relativeOffset));
				entry.Provider->ReadStream(relativeOffset, out.data(), available);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					break;
			} else
				relativeOffset -= entry.BlockSize;

			if (relativeOffset < entry.PadSize) {
				const auto available = std::min(out.size_bytes(), static_cast<size_t>(entry.PadSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty())
					break;
			} else
				relativeOffset -= entry.PadSize;
		}
	}

	return length - out.size_bytes();
}

uint64_t Sqex::Sqpack::VirtualSqPack::SizeIndex1() const {
	return 0ULL +
		m_sqpackIndexHeader.HeaderSize +
		m_sqpackIndexSubHeader.HeaderSize +
		m_sqpackIndexSubHeader.FileSegment.Size +
		m_sqpackIndexSubHeader.DataFilesSegment.Size +
		m_sqpackIndexSubHeader.UnknownSegment3.Size +
		m_sqpackIndexSubHeader.FolderSegment.Size;
}

uint64_t Sqex::Sqpack::VirtualSqPack::SizeIndex2() const {
	return 0ULL +
		m_sqpackIndex2Header.HeaderSize +
		m_sqpackIndex2SubHeader.HeaderSize +
		m_sqpackIndex2SubHeader.FileSegment.Size +
		m_sqpackIndex2SubHeader.DataFilesSegment.Size +
		m_sqpackIndex2SubHeader.UnknownSegment3.Size +
		m_sqpackIndex2SubHeader.FolderSegment.Size;
}

uint64_t Sqex::Sqpack::VirtualSqPack::SizeData(uint32_t datIndex) const {
	if (datIndex >= m_sqpackDataSubHeaders.size())
		return 0;

	return 0ULL +
		m_sqpackDataHeader.HeaderSize +
		m_sqpackDataSubHeaders[datIndex].HeaderSize +
		m_sqpackDataSubHeaders[datIndex].DataSize;
}

class Sqex::Sqpack::VirtualSqPack::Implementation {

};
