#pragma once
#include "Sqex_Sqpack.h"
#include "Sqex_Sqpack_EntryProvider.h"

namespace Sqex::Sqpack {
	class EntryRawStream : public RandomAccessStream {
		struct StreamDecoder {
			EntryRawStream* const stream;

			StreamDecoder(EntryRawStream* stream) : stream(stream) {}

			virtual uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) = 0;
			virtual ~StreamDecoder() = default;
		};
		friend struct StreamDecoder;

		struct BinaryStreamDecoder : StreamDecoder {
			uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
		};

		struct TextureStreamDecoder : StreamDecoder {
			uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
		};

		struct ModelStreamDecoder : StreamDecoder {
			uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) override;
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
