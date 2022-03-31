#pragma once
#include "XivAlexanderCommon/Sqex/Sqpack/StreamDecoder.h"

namespace Sqex::Sqpack {
	class ModelStreamDecoder : public StreamDecoder {
		struct BlockInfo {
			uint32_t RequestOffset;
			uint32_t BlockOffset;
			uint16_t PaddedChunkSize;
			uint16_t DecompressedSize;
			uint16_t GroupIndex;
			uint16_t GroupBlockIndex;
		};

		std::vector<uint8_t> m_head;
		std::vector<BlockInfo> m_blocks;

	public:
		ModelStreamDecoder(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream);
		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) override;
	};
}
