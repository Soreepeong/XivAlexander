#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack.h"
#include "EntryProvider.h"
#include "StreamDecoder.h"

namespace Sqex::Sqpack {
	class StreamDecoder;

	class EntryRawStream : public RandomAccessStream {
		const std::shared_ptr<const EntryProvider> m_provider;
		const SqData::FileEntryHeader m_entryHeader;
		const std::unique_ptr<StreamDecoder> m_decoder;

	public:

		EntryRawStream(std::shared_ptr<const EntryProvider> provider)
			: m_provider(std::move(provider))
			, m_entryHeader(m_provider->ReadStream<SqData::FileEntryHeader>(0))
			, m_decoder(StreamDecoder::CreateNew(m_entryHeader, m_provider)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_decoder ? *m_entryHeader.DecompressedSize : 0;
		}

		[[nodiscard]] SqData::FileEntryType EntryType() const {
			return m_provider->EntryType();
		}

		[[nodiscard]] const EntryPathSpec& PathSpec() const {
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
