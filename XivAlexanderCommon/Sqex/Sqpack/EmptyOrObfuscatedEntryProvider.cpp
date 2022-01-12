#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EmptyOrObfuscatedEntryProvider.h"

Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec)
	: EntryProvider(std::move(pathSpec))
	, m_header(SqData::FileEntryHeader::NewEmpty()) {
}

Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec, uint32_t decompressedSize, std::shared_ptr<const RandomAccessStream> stream)
	: EntryProvider(std::move(pathSpec))
	, m_stream(std::move(stream))
	, m_header(SqData::FileEntryHeader::NewEmpty(decompressedSize, static_cast<size_t>(m_stream->StreamSize()))) {
}

uint64_t Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::StreamSize() const {
	return m_header.HeaderSize + (m_stream ? m_stream->StreamSize() : 0);
}

uint64_t Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (!length)
		return 0;

	auto relativeOffset = offset;
	auto out = std::span(static_cast<char*>(buf), static_cast<size_t>(length));

	if (relativeOffset < sizeof m_header) {
		const auto src = std::span(reinterpret_cast<const char*>(&m_header), sizeof m_header)
			.subspan(static_cast<size_t>(relativeOffset));
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

	return length - out.size_bytes();
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::EntryType() const {
	return SqData::FileEntryType::EmptyOrObfuscated;
}

std::string Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::DescribeState() const {
	return "EmptyOrObfuscatedEntryProvider";
}

const Sqex::Sqpack::EmptyOrObfuscatedEntryProvider& Sqex::Sqpack::EmptyOrObfuscatedEntryProvider::Instance() {
	static const EmptyOrObfuscatedEntryProvider s_instance{ EntryPathSpec() };
	return s_instance;
}
