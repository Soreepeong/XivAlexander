#pragma once
#include <zlib.h>
#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

namespace Sqex::Sqpack {
	class LazyFileOpeningEntryProvider : public EntryProvider {
		mutable std::mutex m_initializationMutex;
		mutable bool m_initialized = false;

	protected:
		const std::filesystem::path m_path;
		const std::shared_ptr<const RandomAccessStream> m_stream;
		const uint64_t m_originalSize;
		const int m_compressionLevel;

	public:
		LazyFileOpeningEntryProvider(EntryPathSpec spec, std::filesystem::path path, bool openImmediately = false, int compressionLevel = Z_BEST_COMPRESSION)
			: EntryProvider(std::move(spec))
			, m_path(std::move(path))
			, m_stream(std::make_shared<FileRandomAccessStream>(m_path, openImmediately))
			, m_originalSize(m_stream->StreamSize())
			, m_compressionLevel(compressionLevel) {
		}

		LazyFileOpeningEntryProvider(EntryPathSpec spec, std::shared_ptr<const RandomAccessStream> stream, int compressionLevel = Z_BEST_COMPRESSION)
			: EntryProvider(std::move(spec))
			, m_path()
			, m_stream(std::move(stream))
			, m_originalSize(m_stream->StreamSize())
			, m_compressionLevel(compressionLevel) {
		}

		[[nodiscard]] std::streamsize StreamSize() const final {
			if (const auto estimate = MaxPossibleStreamSize();
				estimate != SqData::Header::MaxFileSize_MaxValue)
				return estimate;

			ResolveConst();
			return StreamSize(*m_stream);
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const final {
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

		void Resolve() {
			if (m_initialized)
				return;

			const auto lock = std::lock_guard(m_initializationMutex);
			if (m_initialized)
				return;

			Initialize(*m_stream);
			m_initialized = true;
		}

	protected:
		void ResolveConst() const {
			const_cast<LazyFileOpeningEntryProvider*>(this)->Resolve();
		}

		virtual void Initialize(const RandomAccessStream& stream) {
			// does nothing
		}

		[[nodiscard]] virtual uint64_t MaxPossibleStreamSize() const {
			return SqData::Header::MaxFileSize_MaxValue;
		}

		[[nodiscard]] virtual std::streamsize StreamSize(const RandomAccessStream& stream) const = 0;

		virtual std::streamsize ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const = 0;
	};
}
