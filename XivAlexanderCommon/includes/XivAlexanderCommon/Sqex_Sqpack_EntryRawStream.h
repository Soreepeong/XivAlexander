#pragma once
#include "Sqex_Model.h"
#include "Sqex_Sqpack.h"
#include "Sqex_Sqpack_EntryProvider.h"

namespace Sqex::Sqpack {
	class EntryRawStream : public RandomAccessStream {
		struct StreamDecoder {
		private:
			const EntryRawStream* const m_stream;

		public:
			StreamDecoder(const EntryRawStream* stream)
				: m_stream(stream) {
			}

			virtual uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) = 0;
			virtual ~StreamDecoder() = default;

		protected:
			[[nodiscard]] const auto& Underlying() const { return *m_stream->m_provider; }
			[[nodiscard]] const auto& EntryHeader() const { return m_stream->m_entryHeader; }
		};
		friend struct StreamDecoder;

		struct BinaryStreamDecoder : StreamDecoder {
			std::vector<uint32_t> m_offsets;
			std::vector<uint32_t> m_blockOffsets;
			uint16_t MaxBlockSize = sizeof SqData::BlockHeader;

			BinaryStreamDecoder(const EntryRawStream* stream);
			uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
		};

		struct TextureStreamDecoder : StreamDecoder {
			struct BlockInfo {
				uint32_t RequestOffset;
				uint32_t BlockOffset;
				uint16_t MipmapIndex;
				uint32_t RemainingBlocksSize;
				uint32_t RemainingDecompressedSize;
				std::vector<uint16_t> RemainingBlockSizes;
			};
			std::vector<uint8_t> Head;
			std::vector<BlockInfo> Blocks;
			uint16_t MaxBlockSize = sizeof SqData::BlockHeader;

			TextureStreamDecoder(const EntryRawStream* stream);
			uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
		};

		struct ModelStreamDecoder : StreamDecoder {
			struct BlockInfo {
				uint32_t RequestOffset;
				uint32_t BlockOffset;
				uint16_t PaddedChunkSize;
				uint16_t DecompressedSize;
				uint16_t GroupIndex;
				uint16_t GroupBlockIndex;
			};
			std::vector<uint8_t> Head;
			std::vector<BlockInfo> Blocks;
			uint16_t MaxBlockSize = sizeof SqData::BlockHeader;

			ModelStreamDecoder(const EntryRawStream* stream);
			uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;

			Model::Header& AsHeader() { return *reinterpret_cast<Model::Header*>(&Head[0]); }
		};

		const std::shared_ptr<EntryProvider> m_provider;
		const SqData::FileEntryHeader m_entryHeader;
		const std::unique_ptr<StreamDecoder> m_decoder;

	public:
		EntryRawStream(std::shared_ptr<EntryProvider> provider);

		[[nodiscard]] uint64_t StreamSize() const override {
			return m_decoder ? m_entryHeader.DecompressedSize.Value() : 0;
		}

		const EntryPathSpec& PathSpec() const {
			return m_provider->PathSpec();
		}

		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override {
			return m_decoder ? m_decoder->ReadStreamPartial(offset, buf, length) : 0;
		}
	};
}
