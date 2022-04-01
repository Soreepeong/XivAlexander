#ifndef _XIVRES_HOTSWAPPABLEPACKEDFILESTREAM_H_
#define _XIVRES_HOTSWAPPABLEPACKEDFILESTREAM_H_

#include "EmptyOrObfuscatedPackedFileStream.h"

namespace XivRes {
	class HotSwappablePackedFileStream : public PackedFileStream {
		const uint32_t m_reservedSize;
		const std::shared_ptr<const PackedFileStream> m_baseStream;
		std::shared_ptr<const PackedFileStream> m_stream;

	public:
		HotSwappablePackedFileStream(const SqpackPathSpec& pathSpec, uint32_t reservedSize, std::shared_ptr<const PackedFileStream> stream = nullptr)
			: PackedFileStream(pathSpec)
			, m_reservedSize(Align(reservedSize))
			, m_baseStream(std::move(stream)) {
			if (m_baseStream && m_baseStream->StreamSize() > m_reservedSize)
				throw std::invalid_argument("Provided stream requires more space than reserved size");
		}

		std::shared_ptr<const PackedFileStream> SwapStream(std::shared_ptr<const PackedFileStream> newStream = nullptr) {
			if (newStream && newStream->StreamSize() > m_reservedSize)
				throw std::invalid_argument("Provided stream requires more space than reserved size");
			auto oldStream{ std::move(m_stream) };
			m_stream = std::move(newStream);
			return oldStream;
		}

		[[nodiscard]] std::shared_ptr<const PackedFileStream> GetBaseStream() const {
			return m_baseStream;
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_reservedSize;
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset >= m_reservedSize)
				return 0;
			if (offset + length > m_reservedSize)
				length = m_reservedSize - offset;

			auto target = std::span(static_cast<uint8_t*>(buf), static_cast<size_t>(length));
			const auto& underlyingStream = m_stream ? *m_stream : m_baseStream ? *m_baseStream : EmptyOrObfuscatedPackedFileStream::Instance();
			const auto underlyingStreamLength = underlyingStream.StreamSize();
			const auto dataLength = offset < underlyingStreamLength ? (std::min)(length, underlyingStreamLength - offset) : 0;

			if (offset < underlyingStreamLength) {
				const auto dataTarget = target.subspan(0, static_cast<size_t>(dataLength));
				const auto readLength = static_cast<size_t>(underlyingStream.ReadStreamPartial(offset, &dataTarget[0], dataTarget.size_bytes()));
				if (readLength != dataTarget.size_bytes())
					throw std::logic_error("HotSwappableEntryProvider underlying data read fail");
				target = target.subspan(readLength);
			}
			std::ranges::fill(target, 0);
			return length;
		}

		[[nodiscard]] PackedFileType GetPackedFileType() const override {
			return m_stream ? m_stream->GetPackedFileType() : (m_baseStream ? m_baseStream->GetPackedFileType() : EmptyOrObfuscatedPackedFileStream::Instance().GetPackedFileType());
		}
	};
}

#endif
