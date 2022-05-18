#ifndef _XIVRES_LAZYPACKEDFILESTREAM_H_
#define _XIVRES_LAZYPACKEDFILESTREAM_H_

#include <zlib.h>

#include "PackedFileStream.h"

namespace XivRes {
	class LazyPackedFileStream : public PackedFileStream {
		mutable std::mutex m_initializationMutex;
		mutable bool m_initialized = false;

	protected:
		const std::filesystem::path m_path;
		const std::shared_ptr<const IStream> m_stream;
		const uint64_t m_originalSize;
		const int m_compressionLevel;

	public:
		LazyPackedFileStream(SqpackPathSpec spec, std::filesystem::path path, int compressionLevel = Z_BEST_COMPRESSION)
			: PackedFileStream(std::move(spec))
			, m_path(std::move(path))
			, m_stream(std::make_shared<FileStream>(m_path))
			, m_originalSize(m_stream->StreamSize())
			, m_compressionLevel(compressionLevel) {
		}

		LazyPackedFileStream(SqpackPathSpec spec, std::shared_ptr<const IStream> stream, int compressionLevel = Z_BEST_COMPRESSION)
			: PackedFileStream(std::move(spec))
			, m_path()
			, m_stream(std::move(stream))
			, m_originalSize(m_stream->StreamSize())
			, m_compressionLevel(compressionLevel) {
		}

		[[nodiscard]] std::streamsize StreamSize() const final {
			if (const auto estimate = MaxPossibleStreamSize();
				estimate != SqpackDataHeader::MaxFileSize_MaxValue)
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
				const auto read = static_cast<size_t>(ReadStreamPartial(*m_stream, offset, &target[0], (std::min<uint64_t>)(size - offset, target.size())));
				target = target.subspan(read);
			}

			const auto remaining = static_cast<size_t>((std::min<uint64_t>)(estimate - size, target.size()));
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
			const_cast<LazyPackedFileStream*>(this)->Resolve();
		}

		virtual void Initialize(const IStream& stream) {
			// does nothing
		}

		[[nodiscard]] virtual std::streamsize MaxPossibleStreamSize() const {
			return SqpackDataHeader::MaxFileSize_MaxValue;
		}

		[[nodiscard]] virtual std::streamsize StreamSize(const IStream& stream) const = 0;

		virtual std::streamsize ReadStreamPartial(const IStream& stream, std::streamoff offset, void* buf, std::streamsize length) const = 0;
	};
}

#endif
