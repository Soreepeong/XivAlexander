#pragma once

#include "Sqex_Sqpack_EntryProvider.h"

namespace Sqex::Sqpack {
	class EmptyOrObfuscatedEntryProvider : public EntryProvider {
		const std::shared_ptr<const RandomAccessStream> m_stream;
		const SqData::FileEntryHeader m_header;

	public:
		EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec);
		EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec, uint32_t decompressedSize, std::shared_ptr<const RandomAccessStream> stream);

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;
		[[nodiscard]] SqData::FileEntryType EntryType() const override;
		[[nodiscard]] std::string DescribeState() const override;

		static const EmptyOrObfuscatedEntryProvider& Instance();
	};
}
