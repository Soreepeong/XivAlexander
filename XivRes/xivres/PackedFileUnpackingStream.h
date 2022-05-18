#ifndef _XIVRES_PACKEDFILEUNPACKINGSTREAM_H_
#define _XIVRES_PACKEDFILEUNPACKINGSTREAM_H_

#include "PackedFileStream.h"
#include "SqpackStreamDecoder.h"

namespace XivRes {
	class BasePackedFileStreamDecoder;

	class PackedFileUnpackingStream : public DefaultAbstractStream {
		const std::shared_ptr<const PackedFileStream> m_provider;
		const PackedFileHeader m_entryHeader;
		const std::unique_ptr<BasePackedFileStreamDecoder> m_decoder;

	public:
		PackedFileUnpackingStream(std::shared_ptr<const PackedFileStream> provider, std::span<uint8_t> obfuscatedHeaderRewrite = {})
			: m_provider(std::move(provider))
			, m_entryHeader(ReadStream<PackedFileHeader>(*m_provider, 0))
			, m_decoder(BasePackedFileStreamDecoder::CreateNew(m_entryHeader, m_provider, obfuscatedHeaderRewrite)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_decoder ? *m_entryHeader.DecompressedSize : 0;
		}

		[[nodiscard]] PackedFileType PackedFileType() const {
			return m_provider->GetPackedFileType();
		}

		[[nodiscard]] const SqpackPathSpec& PathSpec() const {
			return m_provider->PathSpec();
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!m_decoder)
				return 0;

			const auto fullSize = *m_entryHeader.DecompressedSize;
			if (offset >= fullSize)
				return 0;
			if (offset + length > fullSize)
				length = fullSize - offset;

			const auto decompressedSize = *m_entryHeader.DecompressedSize;
			auto read = m_decoder->ReadStreamPartial(offset, buf, length);
			if (read != length)
				std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
			return length;
		}
	};
}

inline XivRes::PackedFileUnpackingStream XivRes::PackedFileStream::GetUnpackedStream(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return XivRes::PackedFileUnpackingStream(std::static_pointer_cast<const PackedFileStream>(shared_from_this()), obfuscatedHeaderRewrite);
}

inline std::unique_ptr<XivRes::PackedFileUnpackingStream> XivRes::PackedFileStream::GetUnpackedStreamPtr(std::span<uint8_t> obfuscatedHeaderRewrite) const {
	return std::make_unique<XivRes::PackedFileUnpackingStream>(std::static_pointer_cast<const PackedFileStream>(shared_from_this()), obfuscatedHeaderRewrite);
}

#endif
