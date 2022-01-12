#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

namespace Sqex::Sqpack {
	class EmptyOrObfuscatedEntryProvider : public EntryProvider {
		const std::shared_ptr<const RandomAccessStream> m_stream;
		const SqData::FileEntryHeader m_header;

	public:
		EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec);
		EmptyOrObfuscatedEntryProvider(EntryPathSpec pathSpec, std::shared_ptr<const RandomAccessStream> stream, uint32_t decompressedSize = UINT32_MAX);

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;
		[[nodiscard]] SqData::FileEntryType EntryType() const override;
		[[nodiscard]] std::string DescribeState() const override;

		static const EmptyOrObfuscatedEntryProvider& Instance();
	};
}
