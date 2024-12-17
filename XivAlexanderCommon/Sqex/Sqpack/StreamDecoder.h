#pragma once

#include "XivAlexanderCommon/Sqex/Sqpack/EntryProvider.h"
#include "XivAlexanderCommon/Utils/ZlibWrapper.h"

namespace Sqex::Sqpack {
	class StreamDecoder {
	protected:
		struct ReadStreamState {
			const RandomAccessStream& Underlying;
			std::span<uint8_t> TargetBuffer;
			std::vector<uint8_t> ReadBuffer;
			uint64_t RelativeOffset = 0;
			uint32_t RequestOffsetVerify = 0;
			bool HadCompressedBlocks = false;

			ZlibReusableInflater Inflater{ -MAX_WBITS };

			[[nodiscard]] const auto& AsHeader() const {
				return *reinterpret_cast<const SqData::BlockHeader*>(&ReadBuffer[0]);
			}

		private:
			void AttemptSatisfyRequestOffset(const uint32_t requestOffset);

		public:
			void Progress(const uint32_t requestOffset, uint32_t blockOffset);
		};

		const std::shared_ptr<const EntryProvider> m_stream;
		size_t m_maxBlockSize{};

	public:
		StreamDecoder(std::shared_ptr<const EntryProvider> stream)
			: m_stream(std::move(stream))
			, m_maxBlockSize(sizeof(SqData::BlockHeader)) {
		}

		virtual uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) = 0;
		virtual ~StreamDecoder() = default;

		static std::unique_ptr<StreamDecoder> CreateNew(const SqData::FileEntryHeader& header, std::shared_ptr<const EntryProvider> stream);
	};
}
