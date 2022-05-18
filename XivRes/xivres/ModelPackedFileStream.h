#ifndef _XIVRES_MODELPACKEDFILESTREAM_H_
#define _XIVRES_MODELPACKEDFILESTREAM_H_

#include "Internal/ZlibWrapper.h"

#include "LazyPackedFileStream.h"

namespace XivRes {
	class ModelPackedFileViewStream : public LazyPackedFileStream {
		struct ModelEntryHeader {
			PackedFileHeader Entry;
			SqpackModelPackedFileBlockLocator Model;
		} m_header{};
		std::vector<uint32_t> m_blockOffsets;
		std::vector<uint16_t> m_blockDataSizes;
		std::vector<uint16_t> m_paddedBlockSizes;
		std::vector<uint32_t> m_actualFileOffsets;

	public:
		using LazyPackedFileStream::LazyPackedFileStream;
		using LazyPackedFileStream::StreamSize;
		using LazyPackedFileStream::ReadStreamPartial;

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			return PackedFileType::Model;
		}

	protected:
		void Initialize(const IStream& stream) override {
			Model::Header header;
			ReadStream(stream, 0, &header, sizeof header);

			m_header.Entry.Type = PackedFileType::Model;
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
			const auto generateSet = [&](const uint32_t size) {
				const auto alignedDecompressedSize = Align(size).Alloc;
				const auto alignedBlock = Align<uint32_t, uint16_t>(size, EntryBlockDataSize);
				const auto firstBlockOffset = size ? getNextBlockOffset() : 0;
				const auto firstBlockIndex = static_cast<uint16_t>(m_blockOffsets.size());
				alignedBlock.IterateChunked([&](auto, uint32_t offset, uint32_t size) {
					m_blockOffsets.push_back(getNextBlockOffset());
					m_blockDataSizes.push_back(size);
					m_paddedBlockSizes.push_back(static_cast<uint32_t>(Align(sizeof PackedBlockHeader + size)));
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
				if (!header.VertexOffset[i])
					break;

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
			m_header.Entry.SetSpaceUnits(static_cast<size_t>(MaxPossibleStreamSize()));
		}

		[[nodiscard]] std::streamsize MaxPossibleStreamSize() const override {
			const auto blockCount = 11 + Align<uint64_t>(m_originalSize, EntryBlockDataSize).Count;
			const auto headerSize = Align(sizeof m_header + blockCount * sizeof m_blockDataSizes[0]).Alloc;
			return headerSize + blockCount * EntryBlockSize;
		}

		[[nodiscard]] std::streamsize StreamSize(const IStream&) const override { 
			return MaxPossibleStreamSize(); }

		std::streamsize ReadStreamPartial(const IStream& stream, std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!length)
				return 0;

			auto relativeOffset = static_cast<uint64_t>(offset);
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < sizeof m_header) {
				const auto src = Internal::span_cast<char>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= sizeof m_header;

			if (const auto srcTyped = std::span(m_paddedBlockSizes);
				relativeOffset < srcTyped.size_bytes()) {
				const auto src = Internal::span_cast<char>(srcTyped).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= srcTyped.size_bytes();

			if (const auto padBeforeBlocks = Align(sizeof ModelEntryHeader + std::span(m_paddedBlockSizes).size_bytes()).Pad;
				relativeOffset < padBeforeBlocks) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padBeforeBlocks - relativeOffset));
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
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(*it - relativeOffset));
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
				if (relativeOffset < sizeof PackedBlockHeader) {
					const auto header = PackedBlockHeader{
						.HeaderSize = sizeof PackedBlockHeader,
						.Version = 0,
						.CompressedSize = PackedBlockHeader::CompressedSizeNotCompressed,
						.DecompressedSize = m_blockDataSizes[i],
					};
					const auto src = Internal::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
					const auto available = (std::min)(out.size_bytes(), src.size_bytes());
					std::copy_n(src.begin(), available, out.begin());
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= sizeof PackedBlockHeader;

				if (relativeOffset < m_blockDataSizes[i]) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(m_blockDataSizes[i] - relativeOffset));
					ReadStream(stream, m_actualFileOffsets[i] + relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= m_blockDataSizes[i];

				if (const auto padSize = m_paddedBlockSizes[i] - m_blockDataSizes[i] - sizeof PackedBlockHeader;
					relativeOffset < padSize) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(padSize - relativeOffset));
					std::fill_n(out.begin(), available, 0);
					out = out.subspan(static_cast<size_t>(available));
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= padSize;
			}

			if (!out.empty()) {
				const auto actualDataSize = m_header.Entry.HeaderSize + (m_paddedBlockSizes.empty() ? 0 : m_blockOffsets.back() + m_paddedBlockSizes.back());
				const auto endPadSize = static_cast<uint64_t>(MaxPossibleStreamSize() - actualDataSize);
				if (relativeOffset < endPadSize) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
					std::fill_n(out.begin(), available, 0);
					out = out.subspan(static_cast<size_t>(available));
				}
			}

			return length - out.size_bytes();
		}
	};

	class ModelPackedFileStream : public LazyPackedFileStream {
		std::vector<uint8_t> m_data;

	public:
		using LazyPackedFileStream::LazyPackedFileStream;
		using LazyPackedFileStream::StreamSize;
		using LazyPackedFileStream::ReadStreamPartial;

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			return PackedFileType::Model;
		}

	protected:
		void Initialize(const IStream& stream) override {
			Model::Header header;
			ReadStream(stream, 0, &header, sizeof header);

			PackedFileHeader entryHeader{
				.Type = PackedFileType::Model,
				.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
				.BlockCountOrVersion = header.Version,
			};
			SqpackModelPackedFileBlockLocator modelHeader{
				.VertexDeclarationCount = header.VertexDeclarationCount,
				.MaterialCount = header.MaterialCount,
				.LodCount = header.LodCount,
				.EnableIndexBufferStreaming = header.EnableIndexBufferStreaming,
				.EnableEdgeGeometry = header.EnableEdgeGeometry,
			};

			std::optional<Internal::ZlibReusableDeflater> deflater;
			if (m_compressionLevel)
				deflater.emplace(m_compressionLevel, Z_DEFLATED, -15);
			std::vector<uint8_t> entryBody;
			entryBody.reserve(static_cast<size_t>(stream.StreamSize()));

			std::vector<uint32_t> blockOffsets;
			std::vector<uint16_t> paddedBlockSizes;
			const auto getNextBlockOffset = [&]() {
				return paddedBlockSizes.empty() ? 0U : blockOffsets.back() + paddedBlockSizes.back();
			};

			std::vector<uint8_t> tempBuf(EntryBlockDataSize);
			auto baseFileOffset = static_cast<uint32_t>(sizeof header);
			const auto generateSet = [&](const uint32_t size) {
				const auto alignedDecompressedSize = Align(size).Alloc;
				const auto alignedBlock = Align<uint32_t, uint16_t>(size, EntryBlockDataSize);
				const auto firstBlockOffset = size ? getNextBlockOffset() : 0;
				const auto firstBlockIndex = static_cast<uint16_t>(blockOffsets.size());
				alignedBlock.IterateChunked([&](auto, uint32_t offset, uint32_t size) {
					const auto sourceBuf = std::span(tempBuf).subspan(0, size);
					ReadStream(stream, offset, sourceBuf);
					if (deflater)
						deflater->Deflate(sourceBuf);
					const auto useCompressed = deflater && deflater->Result().size() < sourceBuf.size();
					const auto targetBuf = useCompressed ? deflater->Result() : sourceBuf;

					PackedBlockHeader header{
						.HeaderSize = sizeof PackedBlockHeader,
						.Version = 0,
						.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : PackedBlockHeader::CompressedSizeNotCompressed,
						.DecompressedSize = static_cast<uint32_t>(sourceBuf.size()),
					};
					const auto alignmentInfo = Align(sizeof header + targetBuf.size());

					entryBody.resize(entryBody.size() + alignmentInfo.Alloc);

					auto ptr = entryBody.end() - static_cast<size_t>(alignmentInfo.Alloc);
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
				if (!header.VertexOffset[i])
					break;

				baseFileOffset = header.VertexOffset[i];
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
			entryHeader.SetSpaceUnits(entryBody.size());

			m_data.reserve(Align(entryHeader.HeaderSize + entryBody.size()));
			m_data.insert(m_data.end(), reinterpret_cast<char*>(&entryHeader), reinterpret_cast<char*>(&entryHeader + 1));
			m_data.insert(m_data.end(), reinterpret_cast<char*>(&modelHeader), reinterpret_cast<char*>(&modelHeader + 1));
			if (!paddedBlockSizes.empty()) {
				m_data.insert(m_data.end(), reinterpret_cast<char*>(&paddedBlockSizes.front()), reinterpret_cast<char*>(&paddedBlockSizes.back() + 1));
				m_data.resize(entryHeader.HeaderSize, 0);
				m_data.insert(m_data.end(), entryBody.begin(), entryBody.end());
			} else
				m_data.resize(entryHeader.HeaderSize, 0);

			m_data.resize(Align(m_data.size()));
		}

		[[nodiscard]] std::streamsize StreamSize(const IStream& stream) const override {
			return static_cast<uint32_t>(m_data.size()); }

		std::streamsize ReadStreamPartial(const IStream& stream, std::streamoff offset, void* buf, std::streamsize length) const {
			const auto available = static_cast<size_t>((std::min<std::streamsize>)(length, m_data.size() - offset));
			if (!available)
				return 0;

			memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
			return available;
		}
	};
}

#endif
