#pragma once

#include "Sqex_Sqpack_StreamDecoder.h"

namespace Sqex::Sqpack {
	class TextureStreamDecoder : public StreamDecoder {
		struct BlockInfo {
			uint32_t RequestOffset;
			uint32_t BlockOffset;
			uint32_t RemainingDecompressedSize;
			std::vector<uint16_t> RemainingBlockSizes;
		};
		std::vector<uint8_t> m_head;
		std::vector<BlockInfo> m_blocks;

	public:
		TextureStreamDecoder(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream);
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
	};
}
