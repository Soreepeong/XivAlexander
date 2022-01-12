#include "pch.h"
#include "XivAlexanderCommon/Sqex/Sqpack/LazyEntryProvider.h"

Sqex::Sqpack::LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider(EntryPathSpec spec, std::filesystem::path path, bool openImmediately, int compressionLevel)
	: EntryProvider(std::move(spec))
	, m_path(std::move(path))
	, m_stream(std::make_shared<FileRandomAccessStream>(m_path, 0, UINT64_MAX, openImmediately))
	, m_originalSize(m_stream->StreamSize())
	, m_compressionLevel(compressionLevel) {
}

Sqex::Sqpack::LazyFileOpeningEntryProvider::LazyFileOpeningEntryProvider(EntryPathSpec spec, std::shared_ptr<const RandomAccessStream> stream, int compressionLevel)
	: EntryProvider(std::move(spec))
	, m_path()
	, m_stream(std::move(stream))
	, m_originalSize(m_stream->StreamSize())
	, m_compressionLevel(compressionLevel) {
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::StreamSize() const {
	if (const auto estimate = MaxPossibleStreamSize();
		estimate != SqData::Header::MaxFileSize_MaxValue)
		return estimate;

	ResolveConst();
	return StreamSize(*m_stream);
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const {
	ResolveConst();

	const auto size = StreamSize(*m_stream);
	const auto estimate = MaxPossibleStreamSize();
	if (estimate == size || offset + length <= size)
		return ReadStreamPartial(*m_stream, offset, buf, length);

	if (offset >= estimate)
		return 0;
	if (offset + length > estimate)
		length = estimate - offset;

	auto target = std::span(static_cast<char*>(buf), static_cast<size_t>(length));
	if (offset < size) {
		const auto read = static_cast<size_t>(ReadStreamPartial(*m_stream, offset, &target[0], std::min<uint64_t>(size - offset, target.size())));
		target = target.subspan(read);
	}
	// size ... estimate
	const auto remaining = static_cast<size_t>(std::min<uint64_t>(estimate - size, target.size()));
	std::ranges::fill(target.subspan(0, remaining), 0);
	return length - (target.size() - remaining);
}

void Sqex::Sqpack::LazyFileOpeningEntryProvider::Resolve() {
	if (m_initialized)
		return;

	const auto lock = std::lock_guard(m_initializationMutex);
	if (m_initialized)
		return;

	Initialize(*m_stream);
	m_initialized = true;
}

void Sqex::Sqpack::LazyFileOpeningEntryProvider::ResolveConst() const {
	const_cast<LazyFileOpeningEntryProvider*>(this)->Resolve();
}

void Sqex::Sqpack::LazyFileOpeningEntryProvider::Initialize(const RandomAccessStream& stream) {
	// does nothing
}

uint64_t Sqex::Sqpack::LazyFileOpeningEntryProvider::MaxPossibleStreamSize() const {
	return SqData::Header::MaxFileSize_MaxValue;
}
