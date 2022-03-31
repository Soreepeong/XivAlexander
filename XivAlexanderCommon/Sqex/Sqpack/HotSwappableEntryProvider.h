#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EmptyOrObfuscatedEntryProvider.h"

namespace Sqex::Sqpack {
	class HotSwappableEntryProvider : public EntryProvider {
		const uint32_t m_reservedSize;
		const std::shared_ptr<const EntryProvider> m_baseStream;
		std::shared_ptr<const EntryProvider> m_stream;

	public:
		HotSwappableEntryProvider(const EntryPathSpec& pathSpec, uint32_t reservedSize, std::shared_ptr<const EntryProvider> stream = nullptr)
			: EntryProvider(pathSpec)
			, m_reservedSize(Align(reservedSize))
			, m_baseStream(std::move(stream)) {
			if (m_baseStream && m_baseStream->StreamSize() > m_reservedSize)
				throw std::invalid_argument("Provided stream requires more space than reserved size");
		}

		std::shared_ptr<const EntryProvider> SwapStream(std::shared_ptr<const EntryProvider> newStream = nullptr) {
			if (newStream && newStream->StreamSize() > m_reservedSize)
				throw std::invalid_argument("Provided stream requires more space than reserved size");
			auto oldStream{ std::move(m_stream) };
			m_stream = std::move(newStream);
			return oldStream;
		}

		[[nodiscard]] std::shared_ptr<const EntryProvider> GetBaseStream() const {
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
			const auto& underlyingStream = m_stream ? *m_stream : m_baseStream ? *m_baseStream : EmptyOrObfuscatedEntryProvider::Instance();
			const auto underlyingStreamLength = underlyingStream.StreamSize();
			const auto dataLength = offset < underlyingStreamLength ? std::min(length, underlyingStreamLength - offset) : 0;

			if (offset < underlyingStreamLength) {
				const auto dataTarget = target.subspan(0, static_cast<size_t>(dataLength));
				const auto readLength = static_cast<size_t>(underlyingStream.ReadStreamPartial(offset, &dataTarget[0], dataTarget.size_bytes()));
				if (readLength != dataTarget.size_bytes())
					throw std::logic_error(std::format("HotSwappableEntryProvider underlying data read fail: {}", underlyingStream.DescribeState()));
				target = target.subspan(readLength);
			}
			std::ranges::fill(target, 0);
			return length;
		}

		[[nodiscard]] SqData::FileEntryType EntryType() const override {
			return m_stream ? m_stream->EntryType() : (m_baseStream ? m_baseStream->EntryType() : EmptyOrObfuscatedEntryProvider::Instance().EntryType());
		}
	};
}
