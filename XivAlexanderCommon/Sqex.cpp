#include "pch.h"
#include "Sqex.h"

Sqex::RandomAccessStream::RandomAccessStream() = default;

Sqex::RandomAccessStream::~RandomAccessStream() = default;

uint64_t Sqex::RandomAccessStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	return ReadStreamPartial(offset, buf, length);
}

void Sqex::RandomAccessStream::ReadStream(uint64_t offset, void* buf, uint64_t length) const {
	if (ReadStreamPartial(offset, buf, length) != length)
		throw std::runtime_error("Reached end of stream before reading all of the requested data.");
}

Sqex::FileRandomAccessStream::FileRandomAccessStream(Utils::Win32::File file, uint64_t offset, uint64_t length)
	: m_file(std::move(file))
	, m_offset(offset)
	, m_size(length == UINT64_MAX ? m_file.GetLength() - m_offset : length) {
}

Sqex::FileRandomAccessStream::~FileRandomAccessStream() = default;

uint64_t Sqex::FileRandomAccessStream::StreamSize() const {
	return m_size;
}

uint64_t Sqex::FileRandomAccessStream::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	if (offset >= m_size)
		return 0;

	return m_file.Read(m_offset + offset, buf, static_cast<size_t>(std::min(length, m_size - offset)));
}
