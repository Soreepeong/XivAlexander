#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

namespace Sqex::Sqpack {
	class EmptyOrObfuscatedEntryProvider : public EntryProvider {
		const std::shared_ptr<const RandomAccessStream> m_stream;
		const SqData::FileEntryHeader m_header;

	public:
		EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec)
			: EntryProvider(std::move(pathSpec))
			, m_header(SqData::FileEntryHeader::NewEmpty()) {
		}

		EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec, std::shared_ptr<const RandomAccessStream> stream, uint32_t decompressedSize = UINT32_MAX)
			: EntryProvider(std::move(pathSpec))
			, m_stream(std::move(stream))
			, m_header(SqData::FileEntryHeader::NewEmpty(decompressedSize == UINT32_MAX ? static_cast<uint32_t>(m_stream->StreamSize()) : decompressedSize, static_cast<size_t>(m_stream->StreamSize()))) {
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
				const auto src = span_cast<uint8_t>(1, &m_header).subspan(static_cast<size_t>(relativeOffset));
				const auto available = std::min(out.size_bytes(), src.size_bytes());
				std::copy_n(src.begin(), available, out.begin());
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= sizeof m_header;

			if (const auto afterHeaderPad = m_header.HeaderSize - sizeof m_header;
				relativeOffset < afterHeaderPad) {
				const auto available = std::min(out.size_bytes(), afterHeaderPad);
				std::fill_n(out.begin(), available, 0);
				out = out.subspan(available);
				relativeOffset = 0;

				if (out.empty()) return length;
			} else
				relativeOffset -= afterHeaderPad;

			if (const auto dataSize = m_stream ? m_stream->StreamSize() : 0) {
				if (relativeOffset < dataSize) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(dataSize - relativeOffset));
					m_stream->ReadStreamPartial(relativeOffset, &out[0], available);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= dataSize;
			}

			if (const auto pad = m_stream ? Align(m_stream->StreamSize()).Pad : 0) {
				if (relativeOffset < pad) {
					const auto available = std::min(out.size_bytes(), static_cast<size_t>(pad));
					std::fill_n(out.begin(), available, 0);
					out = out.subspan(available);
					relativeOffset = 0;

					if (out.empty()) return length;
				} else
					relativeOffset -= pad;
			}

			return length - out.size_bytes();
		}

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return SqData::FileEntryType::EmptyOrObfuscated;
		}

		static const EmptyOrObfuscatedEntryProvider& Instance() {
			static const EmptyOrObfuscatedEntryProvider s_instance{ EntryPathSpec() };
			return s_instance;
		}
	};
}
