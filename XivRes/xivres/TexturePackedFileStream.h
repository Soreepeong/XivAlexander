#ifndef _XIVRES_TEXTUREPACKEDFILESTREAM_H_
#define _XIVRES_TEXTUREPACKEDFILESTREAM_H_

#include "Internal/SpanCast.h"
#include "Internal/ZlibWrapper.h"

#include "LazyPackedFileStream.h"
#include "Texture.h"

namespace XivRes {
	class TexturePackedFileViewStream : public LazyPackedFileStream {
		static constexpr auto MaxMipmapCountPerTexture = 16;
		/*
		 * [MergedHeader]
		 * - [FileEntryHeader]
		 * - [TextureBlockHeaderLocator] * FileEntryHeader.BlockCount
		 * - SubBlockSize: [uint16_t] * TextureBlockHeaderLocator.SubBlockCount * FileEntryHeader.BlockCount
		 * - [TextureHeaderBytes]
		 * - - [TextureHeader]
		 * - - MipmapOffset: [uint32_t] * TextureHeader.MipmapCount
		 * - - [ExtraHeader]
		 * [BlockHeader, Data] * TextureBlockHeaderLocator.SubBlockCount * TextureHeader.MipmapCount
		 */
		std::vector<SqpackTexturePackedFileBlockLocator> m_blockLocators;
		std::vector<uint16_t> m_subBlockSizes;
		std::vector<uint8_t> m_texHeaderBytes;

		std::vector<uint8_t> m_mergedHeader;

		std::vector<uint32_t> m_mipmapSizes;
		size_t m_size = 0;

	public:
		using LazyPackedFileStream::LazyPackedFileStream;
		using LazyPackedFileStream::StreamSize;
		using LazyPackedFileStream::ReadStreamPartial;

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			return PackedFileType::Texture;
		}

	protected:
		void Initialize(const IStream& stream) override {
			const auto AsTexHeader = [&]() { return *reinterpret_cast<const TextureHeader*>(&m_texHeaderBytes[0]); };
			const auto AsMipmapOffsets = [&]() { return Internal::span_cast<uint32_t>(m_texHeaderBytes, sizeof TextureHeader, *AsTexHeader().MipmapCount); };

			auto entryHeader = PackedFileHeader{
				.HeaderSize = sizeof PackedFileHeader,
				.Type = PackedFileType::Texture,
				.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
			};

			m_texHeaderBytes.resize(sizeof TextureHeader);
			ReadStream(stream, 0, std::span(m_texHeaderBytes));

			m_texHeaderBytes.resize(sizeof TextureHeader + AsTexHeader().MipmapCount * sizeof uint32_t);
			ReadStream(stream, sizeof TextureHeader, std::span(
				reinterpret_cast<uint32_t*>(&m_texHeaderBytes[sizeof TextureHeader]),
				*AsTexHeader().MipmapCount
			));

			m_texHeaderBytes.resize(AsMipmapOffsets()[0]);
			ReadStream(stream, 0, std::span(m_texHeaderBytes).subspan(0, AsMipmapOffsets()[0]));

			std::vector<uint32_t> mipmapOffsets(AsMipmapOffsets().begin(), AsMipmapOffsets().end());;
			m_mipmapSizes.resize(mipmapOffsets.size());
			const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (size_t{} + mipmapOffsets[1] - mipmapOffsets[0]) / TextureRawDataLength(AsTexHeader(), 0);
			for (size_t i = 0; i < mipmapOffsets.size(); ++i)
				m_mipmapSizes[i] = static_cast<uint32_t>(TextureRawDataLength(AsTexHeader(), i));

			// Actual data exists but the mipmap offset array after texture header does not bother to refer
			// to the ones after the first set of mipmaps?
			// For example: if there are mipmaps of 4x4, 2x2, 1x1, 4x4, 2x2, 1x2, 4x4, 2x2, and 1x1,
			// then it will record mipmap offsets only up to the first occurrence of 1x1.
			for (auto forceQuit = false; !forceQuit && (mipmapOffsets.empty() || mipmapOffsets.back() + m_mipmapSizes.back() * repeatCount < entryHeader.DecompressedSize);) {
				for (size_t i = 0, i_ = AsTexHeader().MipmapCount; i < i_; ++i) {

					// <caused by TexTools export>
					const auto size = static_cast<uint32_t>(TextureRawDataLength(AsTexHeader(), i));
					if (mipmapOffsets.back() + m_mipmapSizes.back() + size > entryHeader.DecompressedSize) {
						forceQuit = true;
						break;
					}
					// </caused by TexTools export>

					mipmapOffsets.push_back(mipmapOffsets.back() + m_mipmapSizes.back());
					m_mipmapSizes.push_back(static_cast<uint32_t>(TextureRawDataLength(AsTexHeader(), i)));
				}
			}

			auto blockOffsetCounter = static_cast<uint32_t>(std::span(m_texHeaderBytes).size_bytes());
			for (size_t i = 0; i < mipmapOffsets.size(); ++i) {
				const auto mipmapSize = m_mipmapSizes[i];
				for (uint32_t repeatI = 0; repeatI < repeatCount; repeatI++) {
					const auto blockAlignment = Align<uint32_t>(mipmapSize, EntryBlockDataSize);
					SqpackTexturePackedFileBlockLocator loc{
						.FirstBlockOffset = blockOffsetCounter,
						.TotalSize = 0,
						.DecompressedSize = mipmapSize,
						.FirstSubBlockIndex = m_blockLocators.empty() ? 0 : m_blockLocators.back().FirstSubBlockIndex + m_blockLocators.back().SubBlockCount,
						.SubBlockCount = blockAlignment.Count,
					};
					blockAlignment.IterateChunked([&](uint32_t, const uint32_t offset, const uint32_t length) {
						PackedBlockHeader header{
							.HeaderSize = sizeof PackedBlockHeader,
							.Version = 0,
							.CompressedSize = PackedBlockHeader::CompressedSizeNotCompressed,
							.DecompressedSize = length,
						};
						const auto alignmentInfo = Align(sizeof header + length);

						m_size += alignmentInfo.Alloc;
						m_subBlockSizes.push_back(static_cast<uint16_t>(alignmentInfo.Alloc));
						blockOffsetCounter += m_subBlockSizes.back();
						loc.TotalSize += m_subBlockSizes.back();

					}, mipmapOffsets[i] + m_mipmapSizes[i] * repeatI);

					m_blockLocators.emplace_back(loc);
				}
			}

			entryHeader.BlockCountOrVersion = static_cast<uint32_t>(m_blockLocators.size());
			entryHeader.HeaderSize = static_cast<uint32_t>(XivRes::Align(
				sizeof entryHeader +
				std::span(m_blockLocators).size_bytes() +
				std::span(m_subBlockSizes).size_bytes()));
			entryHeader.SetSpaceUnits(m_size);

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
		}

		[[nodiscard]] std::streamsize MaxPossibleStreamSize() const override {
			const auto blockCount = MaxMipmapCountPerTexture + Align<uint64_t>(m_originalSize, EntryBlockDataSize).Count;
			const auto headerSize = sizeof PackedFileHeader
				+ blockCount * sizeof m_subBlockSizes[0]
				+ MaxMipmapCountPerTexture * sizeof m_blockLocators[0];
			return headerSize + blockCount * EntryBlockSize;
		}

		[[nodiscard]] std::streamsize StreamSize(const IStream&) const override {
			return static_cast<uint32_t>(m_size);
		}

		std::streamsize ReadStreamPartial(const IStream& stream, std::streamoff offset, void* buf, std::streamsize length) const override {
			const auto AsTexHeader = [&]() { return *reinterpret_cast<const TextureHeader*>(&m_texHeaderBytes[0]); };
			const auto AsMipmapOffsets = [&]() { return span_cast<uint32_t>(m_texHeaderBytes, sizeof TextureHeader, AsTexHeader().MipmapCount); };

			if (!length)
				return 0;

			auto relativeOffset = static_cast<uint64_t>(offset);
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < m_mergedHeader.size()) {
				const auto src = std::span(m_mergedHeader)
					.subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= m_mergedHeader.size();

			if (relativeOffset < m_size - m_mergedHeader.size()) {
				relativeOffset += std::span(m_texHeaderBytes).size_bytes();
				auto it = std::lower_bound(m_blockLocators.begin(), m_blockLocators.end(),
					SqpackTexturePackedFileBlockLocator{ .FirstBlockOffset = static_cast<uint32_t>(relativeOffset) },
					[&](const SqpackTexturePackedFileBlockLocator& l, const SqpackTexturePackedFileBlockLocator& r) {
					return l.FirstBlockOffset < r.FirstBlockOffset;
				});

				if (it == m_blockLocators.end())
					--it;
				while (it->FirstBlockOffset > relativeOffset) {
					if (it == m_blockLocators.begin()) {
						const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(it->FirstBlockOffset - relativeOffset));
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
					auto j = relativeOffset / (sizeof PackedBlockHeader + EntryBlockDataSize);
					relativeOffset -= j * (sizeof PackedBlockHeader + EntryBlockDataSize);
					for (; j < it->SubBlockCount; ++j) {
						const auto decompressedSize = j == it->SubBlockCount - 1 ? m_mipmapSizes[blockIndex] % EntryBlockDataSize : EntryBlockDataSize;
						const auto pad = Align(sizeof PackedBlockHeader + decompressedSize).Pad;

						if (relativeOffset < sizeof PackedBlockHeader) {
							const auto header = PackedBlockHeader{
								.HeaderSize = sizeof PackedBlockHeader,
								.Version = 0,
								.CompressedSize = PackedBlockHeader::CompressedSizeNotCompressed,
								.DecompressedSize = decompressedSize,
							};
							const auto src = Internal::span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
							const auto available = (std::min)(out.size_bytes(), src.size_bytes());
							std::copy_n(src.begin(), available, out.begin());
							out = out.subspan(available);
							relativeOffset = 0;

							if (out.empty()) return length;
						} else
							relativeOffset -= sizeof PackedBlockHeader;

						if (relativeOffset < decompressedSize) {
							const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(decompressedSize - relativeOffset));
							ReadStream(stream, AsMipmapOffsets()[blockIndex] + j * EntryBlockDataSize + relativeOffset, &out[0], available);
							out = out.subspan(available);
							relativeOffset = 0;

							if (out.empty()) return length;
						} else
							relativeOffset -= decompressedSize;

						if (relativeOffset < pad) {
							const auto available = (std::min)(out.size_bytes(), pad);
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

			if (const auto endPadSize = static_cast<uint64_t>(StreamSize() - StreamSize(stream)); relativeOffset < endPadSize) {
				const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(endPadSize - relativeOffset));
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(static_cast<size_t>(available));
			}

			return length - out.size_bytes();
		}
	};

	class TexturePackedFileStream : public LazyPackedFileStream {
		std::vector<uint8_t> m_data;

	public:
		using LazyPackedFileStream::LazyPackedFileStream;
		using LazyPackedFileStream::StreamSize;
		using LazyPackedFileStream::ReadStreamPartial;

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			return PackedFileType::Texture;
		}

	protected:
		void Initialize(const IStream& stream) override {
			std::vector<SqpackTexturePackedFileBlockLocator> blockLocators;
			std::vector<uint8_t> readBuffer(EntryBlockDataSize);
			std::vector<uint16_t> subBlockSizes;
			std::vector<uint8_t> texHeaderBytes;

			auto AsTexHeader = [&]() { return *reinterpret_cast<const TextureHeader*>(&texHeaderBytes[0]); };
			auto AsMipmapOffsets = [&]() { return span_cast<uint32_t>(texHeaderBytes, sizeof TextureHeader, AsTexHeader().MipmapCount); };

			auto entryHeader = PackedFileHeader{
				.Type = PackedFileType::Texture,
				.DecompressedSize = static_cast<uint32_t>(stream.StreamSize()),
			};

			texHeaderBytes.resize(sizeof TextureHeader);
			ReadStream(stream, 0, std::span(texHeaderBytes));

			texHeaderBytes.resize(sizeof TextureHeader + AsTexHeader().MipmapCount * sizeof uint32_t);
			ReadStream(stream, sizeof TextureHeader, std::span(
				reinterpret_cast<uint32_t*>(&texHeaderBytes[sizeof TextureHeader]),
				*AsTexHeader().MipmapCount
			));

			texHeaderBytes.resize(AsMipmapOffsets()[0]);
			ReadStream(stream, 0, std::span(texHeaderBytes).subspan(0, AsMipmapOffsets()[0]));

			std::vector<uint32_t> mipmapOffsets(AsMipmapOffsets().begin(), AsMipmapOffsets().end());;
			std::vector<uint32_t> mipmapSizes(mipmapOffsets.size());
			const auto repeatCount = mipmapOffsets.size() < 2 ? 1 : (size_t{} + mipmapOffsets[1] - mipmapOffsets[0]) / TextureRawDataLength(AsTexHeader(), 0);
			for (size_t i = 0; i < mipmapOffsets.size(); ++i)
				mipmapSizes[i] = static_cast<uint32_t>(TextureRawDataLength(AsTexHeader(), i));

			// See above OnTheFlyTextureEntryProvider for comments.
			for (auto forceQuit = false; !forceQuit && (mipmapOffsets.empty() || mipmapOffsets.back() + mipmapSizes.back() * repeatCount < entryHeader.DecompressedSize);) {
				for (size_t i = 0, i_ = AsTexHeader().MipmapCount; i < i_; ++i) {

					// <caused by TexTools export>
					const auto size = static_cast<uint32_t>(TextureRawDataLength(AsTexHeader(), i));
					if (mipmapOffsets.back() + mipmapSizes.back() + size > entryHeader.DecompressedSize) {
						forceQuit = true;
						break;
					}
					// </caused by TexTools export>

					mipmapOffsets.push_back(mipmapOffsets.back() + mipmapSizes.back());
					mipmapSizes.push_back(size);
				}
			}

			std::optional<Internal::ZlibReusableDeflater> deflater;
			if (m_compressionLevel)
				deflater.emplace(m_compressionLevel, Z_DEFLATED, -15);
			std::vector<uint8_t> entryBody;
			entryBody.reserve(static_cast<size_t>(stream.StreamSize()));

			auto blockOffsetCounter = static_cast<uint32_t>(std::span(texHeaderBytes).size_bytes());
			for (size_t i = 0; i < mipmapOffsets.size(); ++i) {
				uint32_t maxMipmapSize = 0;

				const auto minSize = (std::max)(4U, static_cast<uint32_t>(TextureRawDataLength(AsTexHeader().Type, 1, 1, AsTexHeader().Depth, i)));
				if (mipmapSizes[i] > minSize) {
					for (size_t repeatI = 0; repeatI < repeatCount; repeatI++) {
						size_t offset = mipmapOffsets[i] + mipmapSizes[i] * repeatI;
						auto mipmapSize = mipmapSizes[i];
						readBuffer.resize(mipmapSize);

						if (const auto read = static_cast<size_t>(stream.ReadStreamPartial(offset, &readBuffer[0], mipmapSize)); read != mipmapSize) {
							// <caused by TexTools export>
							std::fill_n(&readBuffer[read], mipmapSize - read, 0);
							// </caused by TexTools export>
						}

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
						maxMipmapSize = (std::max)(maxMipmapSize, mipmapSize);
					}
				} else {
					maxMipmapSize = mipmapSizes[i];
				}

				readBuffer.resize(EntryBlockDataSize);
				for (uint32_t repeatI = 0; repeatI < repeatCount; repeatI++) {
					const auto blockAlignment = Align<uint32_t>(maxMipmapSize, EntryBlockDataSize);

					SqpackTexturePackedFileBlockLocator loc{
						.FirstBlockOffset = blockOffsetCounter,
						.TotalSize = 0,
						.DecompressedSize = maxMipmapSize,
						.FirstSubBlockIndex = blockLocators.empty() ? 0 : blockLocators.back().FirstSubBlockIndex + blockLocators.back().SubBlockCount,
						.SubBlockCount = blockAlignment.Count,
					};

					blockAlignment.IterateChunked([&](uint32_t, const uint32_t offset, const uint32_t length) {
						const auto sourceBuf = std::span(readBuffer).subspan(0, length);
						if (const auto read = static_cast<size_t>(stream.ReadStreamPartial(offset, &sourceBuf[0], length)); read != length) {
							// <caused by TexTools export>
							std::fill_n(&sourceBuf[read], length - read, 0);
							// </caused by TexTools export>
						}

						if (deflater)
							deflater->Deflate(sourceBuf);
						const auto useCompressed = deflater && deflater->Result().size() < sourceBuf.size();
						const auto targetBuf = useCompressed ? deflater->Result() : sourceBuf;

						PackedBlockHeader header{
							.HeaderSize = sizeof PackedBlockHeader,
							.Version = 0,
							.CompressedSize = useCompressed ? static_cast<uint32_t>(targetBuf.size()) : PackedBlockHeader::CompressedSizeNotCompressed,
							.DecompressedSize = length,
						};
						const auto alignmentInfo = Align(sizeof header + targetBuf.size());

						subBlockSizes.push_back(static_cast<uint16_t>(alignmentInfo.Alloc));
						blockOffsetCounter += subBlockSizes.back();
						loc.TotalSize += subBlockSizes.back();

						entryBody.resize(entryBody.size() + alignmentInfo.Alloc);
						auto ptr = entryBody.end() - static_cast<size_t>(alignmentInfo.Alloc);
						ptr = std::copy_n(reinterpret_cast<uint8_t*>(&header), sizeof header, ptr);
						ptr = std::copy(targetBuf.begin(), targetBuf.end(), ptr);
						std::fill_n(ptr, alignmentInfo.Pad, 0);
					}, mipmapOffsets[i] + mipmapSizes[i] * repeatI);

					blockLocators.emplace_back(loc);
				}
			}

			entryHeader.BlockCountOrVersion = static_cast<uint32_t>(blockLocators.size());
			entryHeader.HeaderSize = static_cast<uint32_t>(XivRes::Align(
				sizeof entryHeader +
				std::span(blockLocators).size_bytes() +
				std::span(subBlockSizes).size_bytes()));
			entryHeader.SetSpaceUnits(texHeaderBytes.size() + entryBody.size());

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
		}

		[[nodiscard]] std::streamsize StreamSize(const IStream& stream) const override {
			return static_cast<uint32_t>(m_data.size());
		}

		std::streamsize ReadStreamPartial(const IStream& stream, std::streamoff offset, void* buf, std::streamsize length) const override {
			const auto available = static_cast<size_t>((std::min<std::streamsize>)(length, m_data.size() - offset));
			if (!available)
				return 0;

			memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
			return available;
		}
	};
}

#endif
