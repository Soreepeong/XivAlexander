#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack/LazyEntryProvider.h"
#include "XivAlexanderCommon/Utils/ZlibWrapper.h"

namespace Sqex::Sqpack {
	class OnTheFlyBinaryEntryProvider : public LazyFileOpeningEntryProvider {
		const std::vector<uint8_t> m_header;

		uint32_t m_padBeforeData = 0;

	public:
		OnTheFlyBinaryEntryProvider(EntryPathSpec pathSpec, std::filesystem::path path, bool openImmediately = false, int compressionLevel = Z_BEST_COMPRESSION)
			: LazyFileOpeningEntryProvider(std::move(pathSpec), std::move(path), openImmediately, compressionLevel)
			, m_header(CreateHeaderForNonCompressedBinaryEntryProvider(static_cast<size_t>(m_stream->StreamSize()))) {
		}

		OnTheFlyBinaryEntryProvider(EntryPathSpec pathSpec, std::shared_ptr<const RandomAccessStream> stream, int compressionLevel = Z_BEST_COMPRESSION)
			: LazyFileOpeningEntryProvider(std::move(pathSpec), std::move(stream), compressionLevel)
			, m_header(CreateHeaderForNonCompressedBinaryEntryProvider(static_cast<size_t>(m_stream->StreamSize()))) {
		}

		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Binary; }

	protected:
		[[nodiscard]] uint64_t MaxPossibleStreamSize() const override {
			return reinterpret_cast<const SqData::FileEntryHeader*>(&m_header[0])->GetTotalEntrySize();
		}

		[[nodiscard]] std::streamsize StreamSize(const RandomAccessStream& stream) const override { return MaxPossibleStreamSize(); }

		std::streamsize ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const override {
			if (!length)
				return 0;

			const auto& header = *reinterpret_cast<const SqData::FileEntryHeader*>(&m_header[0]);

			auto relativeOffset = offset;
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < m_header.size()) {
				const auto src = std::span(m_header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= m_header.size();

			const auto blockAlignment = Align<uint32_t>(static_cast<uint32_t>(m_stream->StreamSize()), EntryBlockDataSize);
			if (static_cast<uint32_t>(relativeOffset) < header.OccupiedSpaceUnitCount * EntryAlignment) {
				const auto i = relativeOffset / EntryBlockSize;
				relativeOffset -= i * EntryBlockSize;

				blockAlignment.IterateChunkedBreakable([&](uint32_t, uint32_t offset, uint32_t size) {
					if (relativeOffset < sizeof SqData::BlockHeader) {
						const auto header = SqData::BlockHeader{
							.HeaderSize = sizeof SqData::BlockHeader,
							.Version = 0,
							.CompressedSize = SqData::BlockHeader::CompressedSizeNotCompressed,
							.DecompressedSize = static_cast<uint32_t>(size),
						};
						const auto src = span_cast<uint8_t>(1, &header).subspan(static_cast<size_t>(relativeOffset));
						const auto available = std::min(out.size_bytes(), src.size_bytes());
						std::copy_n(src.begin(), available, out.begin());
						out = out.subspan(available);
						relativeOffset = 0;

						if (out.empty()) return false;
					} else
						relativeOffset -= sizeof SqData::BlockHeader;

					if (relativeOffset < size) {
						const auto available = std::min(out.size_bytes(), static_cast<size_t>(size - relativeOffset));
						stream.ReadStream(offset + relativeOffset, &out[0], available);
						out = out.subspan(available);
						relativeOffset = 0;

						if (out.empty()) return false;
					} else
						relativeOffset -= size;

					if (const auto pad = Align(sizeof SqData::BlockHeader + size).Pad; relativeOffset < pad) {
						const auto available = std::min(out.size_bytes(), static_cast<size_t>(pad - relativeOffset));
						std::fill_n(out.begin(), available, 0);
						out = out.subspan(static_cast<size_t>(available));
						relativeOffset = 0;

						if (out.empty()) return false;
					} else
						relativeOffset -= pad;

					return true;
					}, 0, static_cast<uint32_t>(i));
			}

			return length - out.size_bytes();
		}

	private:
		static std::vector<uint8_t> CreateHeaderForNonCompressedBinaryEntryProvider(size_t size) {
			const auto blockAlignment = Align<uint32_t>(static_cast<uint32_t>(size), EntryBlockDataSize);
			const auto headerAlignment = Align(sizeof SqData::FileEntryHeader + blockAlignment.Count * sizeof SqData::BlockHeaderLocator);

			std::vector<uint8_t> res(headerAlignment.Alloc);
			auto& header = *reinterpret_cast<SqData::FileEntryHeader*>(&res[0]);
			const auto locators = span_cast<SqData::BlockHeaderLocator>(res, sizeof header, blockAlignment.Count);

			header = {
				.HeaderSize = static_cast<uint32_t>(headerAlignment),
				.Type = SqData::FileEntryType::Binary,
				.DecompressedSize = static_cast<uint32_t>(size),
				.BlockCountOrVersion = blockAlignment.Count,
			};
			header.SetSpaceUnits((static_cast<size_t>(blockAlignment.Count) - 1) * EntryBlockSize + sizeof SqData::BlockHeader + blockAlignment.Last);

			blockAlignment.IterateChunked([&](uint32_t index, uint32_t offset, uint32_t size) {
				locators[index] = {
					static_cast<uint32_t>(offset),
					static_cast<uint16_t>(EntryBlockSize),
					static_cast<uint16_t>(size)
				};
				});
			return res;
		}
	};

	class MemoryBinaryEntryProvider : public LazyFileOpeningEntryProvider {
		std::vector<char> m_data;

	public:
		using LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider;
		using LazyFileOpeningEntryProvider::StreamSize;
		using LazyFileOpeningEntryProvider::ReadStreamPartial;

		[[nodiscard]] SqData::FileEntryType EntryType() const override { return SqData::FileEntryType::Binary; }

	protected:
		void Initialize(const RandomAccessStream& stream) override {
			const auto rawSize = static_cast<uint32_t>(stream.StreamSize());
			SqData::FileEntryHeader entryHeader = {
				.HeaderSize = sizeof entryHeader,
				.Type = SqData::FileEntryType::Binary,
				.DecompressedSize = rawSize,
				.BlockCountOrVersion = 0,
			};

			std::optional<Utils::ZlibReusableDeflater> deflater;
			if (m_compressionLevel)
				deflater.emplace(m_compressionLevel, Z_DEFLATED, -15);
			std::vector<uint8_t> entryBody;
			entryBody.reserve(rawSize);

			std::vector<SqData::BlockHeaderLocator> locators;
			std::vector<uint8_t> sourceBuf(EntryBlockDataSize);
			Align<uint32_t>(rawSize, EntryBlockDataSize).IterateChunked([&](uint32_t index, uint32_t offset, uint32_t size) {
				sourceBuf.resize(size);
				stream.ReadStream(offset, std::span(sourceBuf));
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

				auto ptr = entryBody.end() - static_cast<size_t>(alignmentInfo.Alloc);
				ptr = std::copy_n(reinterpret_cast<uint8_t*>(&header), sizeof header, ptr);
				ptr = std::copy(targetBuf.begin(), targetBuf.end(), ptr);
				std::fill_n(ptr, alignmentInfo.Pad, 0);
				});

			entryHeader.BlockCountOrVersion = static_cast<uint32_t>(locators.size());
			entryHeader.HeaderSize = static_cast<uint32_t>(Align(entryHeader.HeaderSize + std::span(locators).size_bytes()));
			entryHeader.SetSpaceUnits(entryBody.size());
			m_data.reserve(Align(entryHeader.HeaderSize + entryBody.size()));
			m_data.insert(m_data.end(), reinterpret_cast<char*>(&entryHeader), reinterpret_cast<char*>(&entryHeader + 1));
			if (!locators.empty()) {
				m_data.insert(m_data.end(), reinterpret_cast<char*>(&locators.front()), reinterpret_cast<char*>(&locators.back() + 1));
				m_data.resize(entryHeader.HeaderSize, 0);
				m_data.insert(m_data.end(), entryBody.begin(), entryBody.end());
			} else
				m_data.resize(entryHeader.HeaderSize, 0);

			m_data.resize(Align(m_data.size()));
		}

		[[nodiscard]] std::streamsize StreamSize(const RandomAccessStream& stream) const override { return static_cast<uint32_t>(m_data.size()); }

		std::streamsize ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const override {
			const auto available = static_cast<size_t>(std::min(length, m_data.size() - offset));
			if (!available)
				return 0;

			memcpy(buf, &m_data[static_cast<size_t>(offset)], available);
			return available;
		}
	};
}
