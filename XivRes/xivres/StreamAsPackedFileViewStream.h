#ifndef _XIVRES_STREAMASPACKEDFILESTREAM_H_
#define _XIVRES_STREAMASPACKEDFILESTREAM_H_

#include "PackedFileStream.h"

namespace XivRes {
	class StreamAsPackedFileViewStream : public PackedFileStream {
		const std::shared_ptr<const IStream> m_stream;

		mutable std::optional<PackedFileType> m_entryType;

	public:
		StreamAsPackedFileViewStream(SqpackPathSpec pathSpec, std::shared_ptr<const IStream> stream)
			: PackedFileStream(std::move(pathSpec))
			, m_stream(std::move(stream)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_stream->StreamSize();
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			return m_stream->ReadStreamPartial(offset, buf, length);
		}

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			if (!m_entryType) {
				// operation that should be lightweight enough that lock should not be needed
				m_entryType = ReadStream<PackedFileHeader>(*m_stream, 0).Type;
			}
			return *m_entryType;
		}
	};
}

#endif
