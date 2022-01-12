#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/StreamDecoder.h"

namespace Sqex::Sqpack {
	class BinaryStreamDecoder : public StreamDecoder {
		std::vector<uint32_t> m_offsets;
		std::vector<uint32_t> m_blockOffsets;

	public:
		BinaryStreamDecoder(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream);
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
	};
}
