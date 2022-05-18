#ifndef _XIVRES_BINARYPACKEDFILESTREAMDECODER_H_
#define _XIVRES_BINARYPACKEDFILESTREAMDECODER_H_

#include <ranges>
#include <vector>

#include "BinaryPackedFileStream.h"
#include "Sqpack.h"
#include "SqpackStreamDecoder.h"

namespace XivRes {
	class BinaryPackedFileStreamDecoder : public BasePackedFileStreamDecoder {
		const std::vector<SqpackBinaryPackedFileBlockLocator> m_locators;
		const uint32_t m_headerSize;
		std::vector<uint32_t> m_offsets;

	public:
		BinaryPackedFileStreamDecoder(const PackedFileHeader& header, std::shared_ptr<const PackedFileStream> stream)
			: BasePackedFileStreamDecoder(std::move(stream))
			, m_headerSize(header.HeaderSize)
			, m_locators(ReadStreamIntoVector<SqpackBinaryPackedFileBlockLocator>(*m_stream, sizeof PackedFileHeader, header.BlockCountOrVersion)) {

			m_offsets.resize(m_locators.size() + 1);
			for (size_t i = 1; i < m_offsets.size(); ++i)
				m_offsets[i] = m_offsets[i - 1] + m_locators[i - 1].DecompressedDataSize;
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) override {
			if (!length || m_offsets.empty())
				return 0;

			auto from = static_cast<size_t>(std::distance(m_offsets.begin(), std::ranges::lower_bound(m_offsets, static_cast<uint32_t>(offset))));
			if (from == m_offsets.size())
				return 0;
			if (m_offsets[from] > offset)
				from -= 1;

			ReadStreamState info{
				.TargetBuffer = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length)),
				.RelativeOffset = offset - m_offsets[from],
				.RequestOffsetVerify = m_offsets[from],
			};

			for (auto it = from; it < m_offsets.size(); ++it) {
				info.ProgressRead(*m_stream, m_headerSize + m_locators[it].Offset, m_locators[it].BlockSize);
				info.ProgressDecode(m_offsets[it]);
				if (info.TargetBuffer.empty())
					break;
			}

			return length - info.TargetBuffer.size_bytes();
		}
	};
}

#endif
