
#include "XivAlexanderCommon/Sqex/Sqpack/RandomAccessStreamAsEntryProviderView.h"

Sqex::Sqpack::RandomAccessStreamAsEntryProviderView::RandomAccessStreamAsEntryProviderView(EntryPathSpec pathSpec, std::shared_ptr<const RandomAccessStream> stream, uint64_t offset, uint64_t length)
	: EntryProvider(std::move(pathSpec))
	, m_stream(std::move(stream))
	, m_offset(offset)
	, m_size(length == UINT64_MAX ? m_stream->StreamSize() - m_offset : length) {
	if (m_offset + m_size > m_stream->StreamSize())
		throw std::invalid_argument(std::format("offset({}) + size({}) > file size({} from {})", m_offset, m_size, m_stream->StreamSize(), m_stream->DescribeState()));
}

uint64_t Sqex::Sqpack::RandomAccessStreamAsEntryProviderView::StreamSize() const {
	return m_size;
}

uint64_t Sqex::Sqpack::RandomAccessStreamAsEntryProviderView::ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const {
	if (offset >= m_size)
		return 0;

	return m_stream->ReadStreamPartial(m_offset + offset, buf, static_cast<size_t>(std::min(length, m_size - offset)));
}

Sqex::Sqpack::SqData::FileEntryType Sqex::Sqpack::RandomAccessStreamAsEntryProviderView::EntryType() const {
	if (!m_entryType) {
		// operation that should be lightweight enough that lock should not be needed
		m_entryType = m_stream->ReadStream<SqData::FileEntryHeader>(m_offset).Type;
	}
	return *m_entryType;
}

std::string Sqex::Sqpack::RandomAccessStreamAsEntryProviderView::DescribeState() const {
	return std::format("RandomAccessStreamAsEntryProviderView({}, {}, {})", m_stream->DescribeState(), m_offset, m_size);
}
