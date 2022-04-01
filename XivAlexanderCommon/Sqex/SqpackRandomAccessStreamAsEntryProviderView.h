#pragma once
#include "SqpackEntryProvider.h"

namespace XivRes {
	class RandomAccessStreamAsPackedFileView : public PackedFileStream {
		const std::shared_ptr<const RandomAccessStream> m_stream;

		mutable std::optional<SqData::PackedFileType> m_entryType;

	public:
		RandomAccessStreamAsPackedFileView(SqpackPathSpec pathSpec, std::shared_ptr<const RandomAccessStream> stream)
			: PackedFileStream(std::move(pathSpec))
			, m_stream(std::move(stream)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_stream->StreamSize();
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			return m_stream->ReadStreamPartial(offset, buf, length);
		}

		[[nodiscard]] SqData::PackedFileType PackedFileType() const override {
			if (!m_entryType) {
				// operation that should be lightweight enough that lock should not be needed
				m_entryType = m_stream->ReadStream<SqData::PackedFileHeader>(0).Type;
			}
			return *m_entryType;
		}
	};
}
