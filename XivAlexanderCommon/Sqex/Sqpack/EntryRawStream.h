#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack.h"
#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

namespace Sqex::Sqpack {
	class StreamDecoder;

	class EntryRawStream : public RandomAccessStream {
		const std::shared_ptr<const EntryProvider> m_provider;
		const SqData::FileEntryHeader m_entryHeader;
		const std::unique_ptr<StreamDecoder> m_decoder;

	public:
		EntryRawStream(std::shared_ptr<const EntryProvider> provider);
		~EntryRawStream();

		[[nodiscard]] uint64_t StreamSize() const override;
		[[nodiscard]] SqData::FileEntryType EntryType() const;
		[[nodiscard]] const EntryPathSpec& PathSpec() const;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;
	};
}
