#ifndef _XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAMDECODER_H_
#define _XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAMDECODER_H_

#include "Internal/ZlibWrapper.h"

#include "SqpackStreamDecoder.h"
#include "StreamAsPackedFileViewStream.h"

namespace XivRes {
	class EmptyOrObfuscatedPackedFileStreamDecoder : public BasePackedFileStreamDecoder {
		std::optional<Internal::ZlibReusableInflater> m_inflater;
		std::shared_ptr<PartialViewStream> m_partialView;
		std::optional<StreamAsPackedFileViewStream> m_provider;

	public:
		EmptyOrObfuscatedPackedFileStreamDecoder(const XivRes::PackedFileHeader& header, std::shared_ptr<const XivRes::PackedFileStream> stream, std::span<uint8_t> headerRewrite = {})
			: BasePackedFileStreamDecoder(std::move(stream)) {

			if (header.DecompressedSize < header.BlockCountOrVersion) {
				auto src = ReadStreamIntoVector<uint8_t>(*m_stream, header.HeaderSize, header.DecompressedSize);
				if (!headerRewrite.empty())
					std::copy(headerRewrite.begin(), headerRewrite.begin() + (std::min)(headerRewrite.size(), src.size()), src.begin());
				m_inflater.emplace(-MAX_WBITS, header.DecompressedSize);
				m_provider.emplace(m_stream->PathSpec(), std::make_shared<XivRes::MemoryStream>((*m_inflater)(src)));

			} else {
				m_partialView = std::make_shared<XivRes::PartialViewStream>(m_stream, header.HeaderSize);
				m_provider.emplace(m_stream->PathSpec(), m_partialView);
			}
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) override {
			return m_provider->ReadStreamPartial(offset, buf, length);
		}
	};
}

#endif
