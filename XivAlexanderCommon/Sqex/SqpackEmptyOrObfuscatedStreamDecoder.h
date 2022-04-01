#pragma once
#include "SqpackStreamDecoder.h"
#include "SqpackRandomAccessStreamAsEntryProviderView.h"

#include "internal/ZlibWrapper.h"

namespace XivRes {
	class EmptyPackedFileStreamDecoder : public BasePackedFileStreamDecoder {
		std::optional<Internal::ZlibReusableInflater> m_inflater;
		std::shared_ptr<RandomAccessStreamPartialView> m_partialView;
		std::optional<RandomAccessStreamAsPackedFileView> m_provider;

	public:
		EmptyPackedFileStreamDecoder(const XivRes::SqData::PackedFileHeader& header, std::shared_ptr<const XivRes::PackedFileStream> stream, std::span<uint8_t> headerRewrite = {})
			: BasePackedFileStreamDecoder(std::move(stream)) {

			if (header.DecompressedSize < header.BlockCountOrVersion) {
				auto src = m_stream->ReadStreamIntoVector<uint8_t>(header.HeaderSize, header.DecompressedSize);
				if (!headerRewrite.empty())
					std::copy(headerRewrite.begin(), headerRewrite.begin() + (std::min)(headerRewrite.size(), src.size()), src.begin());
				m_inflater.emplace(-MAX_WBITS, header.DecompressedSize);
				m_provider.emplace(m_stream->PathSpec(), std::make_shared<XivRes::MemoryRandomAccessStream>((*m_inflater)(src)));

			} else {
				m_partialView = std::make_shared<XivRes::RandomAccessStreamPartialView>(m_stream, header.HeaderSize);
				m_provider.emplace(m_stream->PathSpec(), m_partialView);
			}
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) override {
			return m_provider->ReadStreamPartial(offset, buf, length);
		}
	};
}
