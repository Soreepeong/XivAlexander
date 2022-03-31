#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

namespace Sqex::Sqpack {
	class RandomAccessStreamAsEntryProviderView : public EntryProvider {
		const std::shared_ptr<const RandomAccessStream> m_stream;
		const uint64_t m_offset;
		const uint64_t m_size;

		mutable std::optional<SqData::FileEntryType> m_entryType;

	public:
		RandomAccessStreamAsEntryProviderView(EntryPathSpec pathSpec, std::shared_ptr<const RandomAccessStream> stream, uint64_t offset = 0, uint64_t length = UINT64_MAX);

		[[nodiscard]] std::streamsize StreamSize() const override;
		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override;
		[[nodiscard]] SqData::FileEntryType EntryType() const override;
	};
}
