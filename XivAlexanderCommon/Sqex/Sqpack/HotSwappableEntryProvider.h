#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"

namespace Sqex::Sqpack {
	class HotSwappableEntryProvider : public EntryProvider {
		const uint32_t m_reservedSize;
		const std::shared_ptr<const EntryProvider> m_baseStream;
		std::shared_ptr<const EntryProvider> m_stream;

	public:
		HotSwappableEntryProvider(const EntryPathSpec& pathSpec, uint32_t reservedSize, std::shared_ptr<const EntryProvider> stream = nullptr);

		std::shared_ptr<const EntryProvider> SwapStream(std::shared_ptr<const EntryProvider> newStream = nullptr);
		[[nodiscard]] std::shared_ptr<const EntryProvider> GetBaseStream() const;

		[[nodiscard]] uint64_t StreamSize() const override;
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;
		[[nodiscard]] SqData::FileEntryType EntryType() const override;
		[[nodiscard]] std::string DescribeState() const override;
	};
}
