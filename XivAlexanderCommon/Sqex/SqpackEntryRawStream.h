#pragma once

#include "Sqpack.h"
#include "SqpackEntryProvider.h"
#include "SqpackStreamDecoder.h"

namespace XivRes {
	class BasePackedFileStreamDecoder;

	class PackedFileUnpackingStream : public RandomAccessStream {
		const std::shared_ptr<const PackedFileStream> m_provider;
		const SqData::PackedFileHeader m_entryHeader;
		const std::unique_ptr<BasePackedFileStreamDecoder> m_decoder;

	public:
		PackedFileUnpackingStream(std::shared_ptr<const PackedFileStream> provider, std::span<uint8_t> obfuscatedHeaderRewrite = {})
			: m_provider(std::move(provider))
			, m_entryHeader(m_provider->ReadStream<SqData::PackedFileHeader>(0))
			, m_decoder(BasePackedFileStreamDecoder::CreateNew(m_entryHeader, m_provider, obfuscatedHeaderRewrite)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_decoder ? *m_entryHeader.DecompressedSize : 0;
		}

		[[nodiscard]] SqData::PackedFileType PackedFileType() const {
			return m_provider->PackedFileType();
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
