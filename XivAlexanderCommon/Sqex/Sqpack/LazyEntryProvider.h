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
		LazyFileOpeningEntryProvider(EntryPathSpec, std::filesystem::path, bool openImmediately = false, int compressionLevel = Z_BEST_COMPRESSION);
		LazyFileOpeningEntryProvider(EntryPathSpec, std::shared_ptr<const RandomAccessStream>, int compressionLevel = Z_BEST_COMPRESSION);

		[[nodiscard]] uint64_t StreamSize() const final;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const final;

		void Resolve();

	protected:
		void ResolveConst() const;
		virtual void Initialize(const RandomAccessStream& stream);
		[[nodiscard]] virtual uint64_t MaxPossibleStreamSize() const;
		[[nodiscard]] virtual uint64_t StreamSize(const RandomAccessStream& stream) const = 0;
		virtual uint64_t ReadStreamPartial(const RandomAccessStream& stream, uint64_t offset, void* buf, uint64_t length) const = 0;
	};
}
