#ifndef _XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAM_H_
#define _XIVRES_EMPTYOROBFUSCATEDPACKEDFILESTREAM_H_

#include "PackedFileStream.h"

namespace XivRes {
	class EmptyOrObfuscatedPackedFileStream : public PackedFileStream {
		const std::shared_ptr<const IStream> m_stream;
		const PackedFileHeader m_header;

	public:
		EmptyOrObfuscatedPackedFileStream(SqpackPathSpec pathSpec)
			: PackedFileStream(std::move(pathSpec))
			, m_header(PackedFileHeader::NewEmpty()) {
		}

		EmptyOrObfuscatedPackedFileStream(SqpackPathSpec pathSpec, std::shared_ptr<const IStream> stream, uint32_t decompressedSize = UINT32_MAX)
			: PackedFileStream(std::move(pathSpec))
			, m_stream(std::move(stream))
			, m_header(PackedFileHeader::NewEmpty(decompressedSize == UINT32_MAX ? static_cast<uint32_t>(m_stream->StreamSize()) : decompressedSize, static_cast<size_t>(m_stream->StreamSize()))) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_header.HeaderSize + (m_stream ? Align(m_stream->StreamSize()).Alloc : 0);
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (!length)
				return 0;

			auto relativeOffset = offset;
			auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

			if (relativeOffset < sizeof m_header) {
				const auto src = Internal::span_cast<uint8_t>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = (std::min)(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= sizeof m_header;

			if (const auto afterHeaderPad = static_cast<std::streamoff>(m_header.HeaderSize - sizeof m_header);
				relativeOffset < afterHeaderPad) {
				const auto available = (std::min<size_t>)(out.size_bytes(), afterHeaderPad);
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= afterHeaderPad;

			if (const auto dataSize = m_stream ? m_stream->StreamSize() : 0) {
				if (relativeOffset < dataSize) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(dataSize - relativeOffset));
					ReadStream(*m_stream, relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= dataSize;
			}

			if (const auto pad = m_stream ? Align(m_stream->StreamSize()).Pad : 0) {
				if (relativeOffset < pad) {
					const auto available = (std::min)(out.size_bytes(), static_cast<size_t>(pad));
					std::fill_n(out.begin(), available, 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= pad;
			}

			return length - out.size_bytes();
		}

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			return PackedFileType::EmptyOrObfuscated;
		}

		static const EmptyOrObfuscatedPackedFileStream& Instance() {
			static const EmptyOrObfuscatedPackedFileStream s_instance{ SqpackPathSpec() };
			return s_instance;
		}
	};
}

#endif
