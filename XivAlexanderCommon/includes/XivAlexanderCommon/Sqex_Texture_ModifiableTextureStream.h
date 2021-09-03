#pragma once

#include "Sqex_Texture.h"

namespace Sqex::Texture {
	class MipmapStream : public RandomAccessStream {
		const uint16_t m_width;
		const uint16_t m_height;
		const CompressionType m_type;

	public:
		MipmapStream(uint16_t width, uint16_t height, CompressionType type)
			: m_width(width)
			, m_height(height)
			, m_type(type) {
		}
		~MipmapStream() override = default;

		[[nodiscard]] auto Width() const { return m_width; }
		[[nodiscard]] auto Height() const { return m_height; }
		[[nodiscard]] auto Type() const { return m_type; }
	};

	class MemoryBackedMipmap : public MipmapStream {
		std::vector<uint8_t> m_data;

	public:
		MemoryBackedMipmap(uint16_t width, uint16_t height, CompressionType type, std::vector<uint8_t> data)
			: MipmapStream(width, height, type)
			, m_data(std::move(data)) {
		}
		~MemoryBackedMipmap() override = default;
	};

	class FileBackedReadOnlyMipmap : public MipmapStream {
		
	};

	class ModifiableTextureStream : public RandomAccessStream {
		Header m_header;

	public:
		ModifiableTextureStream(uint16_t width, uint16_t height);
		ModifiableTextureStream(const RandomAccessStream& stream, bool strict = false);

		[[nodiscard]] uint32_t StreamSize() const override;
		size_t ReadStreamPartial(uint64_t offset, void* buf, size_t length) const override;
		
	};
}
