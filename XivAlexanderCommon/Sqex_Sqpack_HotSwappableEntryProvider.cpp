#include "pch.h"
#include "Sqex_Sqpack_HotSwappableEntryProvider.h"

#include "Sqex_Sqpack_EmptyOrObfuscatedEntryProvider.h"

Sqex::Sqpack::HotSwappableEntryProvider::HotSwappableEntryProvider(const EntryPathSpec& pathSpec, uint32_t reservedSize, std::shared_ptr<const EntryProvider> stream)
	: EntryProvider(pathSpec)
	, m_reservedSize(Align(reservedSize))
	, m_baseStream(std::move(stream)) {
	if (m_baseStream && m_baseStream->StreamSize() > m_reservedSize)
		throw std::invalid_argument("Provided stream requires more space than reserved size");
}

std::shared_ptr<const Sqex::Sqpack::EntryProvider> Sqex::Sqpack::HotSwappableEntryProvider::SwapStream(std::shared_ptr<const EntryProvider> newStream /*= nullptr*/) {
	if (newStream && newStream->StreamSize() > m_reservedSize)
		throw std::invalid_argument("Provided stream requires more space than reserved size");
	auto oldStream{ std::move(m_stream) };
	m_stream = std::move(newStream);
	return oldStream;
}

std::shared_ptr<const Sqex::Sqpack::EntryProvider> Sqex::Sqpack::HotSwappableEntryProvider::GetBaseStream() const {
	return m_baseStream;
}

uint64_t Sqex::Sqpack::HotSwappableEntryProvider::StreamSize() const {
	return m_reservedSize;
}

uint64_t Sqex::Sqpack::HotSwappableEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (offset >= m_reservedSize)
		return 0;
	if (offset + length > m_reservedSize)
		length = m_reservedSize - offset;

	auto target = std::span(static_cast<uint8_t*>(buf), static_cast<SSIZE_T>(length));
	const auto& underlyingStream = m_stream ? *m_stream : m_baseStream ? *m_baseStream : EmptyOrObfuscatedEntryProvider::Instance();
	const auto underlyingStreamLength = underlyingStream.StreamSize();
	const auto dataLength = offset < underlyingStreamLength ? std::min(length, underlyingStreamLength - offset) : 0;

	if (offset < underlyingStreamLength) {
		const auto dataTarget = target.subspan(0, static_cast<SSIZE_T>(dataLength));
		const auto readLength = static_cast<size_t>(underlyingStream.ReadStreamPartial(offset, &dataTarget[0], dataTarget.size_bytes()));
		if (readLength != dataTarget.size_bytes())
			throw std::logic_error(std::format("HotSwappableEntryProvider underlying data read fail: {}", underlyingStream.DescribeState()));
		target = target.subspan(readLength);
	}
	std::ranges::fill(target, 0);
	return length;
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::HotSwappableEntryProvider::EntryType() const {
	return m_stream ? m_stream->EntryType() : (m_baseStream ? m_baseStream->EntryType() : EmptyOrObfuscatedEntryProvider::Instance().EntryType());
}

std::string Sqex::Sqpack::HotSwappableEntryProvider::DescribeState() const {
	return std::format("HotSwappableEntryProvider(reserved={}, base={}, override={})",
		m_reservedSize,
		m_baseStream ? m_baseStream->DescribeState() : std::string(),
		m_stream ? m_stream->DescribeState() : std::string());
}

