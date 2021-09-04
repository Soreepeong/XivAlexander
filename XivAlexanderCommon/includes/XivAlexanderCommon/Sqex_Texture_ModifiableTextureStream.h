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

		[[nodiscard]] auto Width() const { return m_width; }
		[[nodiscard]] auto Height() const { return m_height; }
		[[nodiscard]] auto Type() const { return m_type; }

		std::shared_ptr<const MipmapStream> ViewARGB8888(CompressionType type = CompressionType::ARGB_1) const;
	};

	class WrappedMipmapStream : public MipmapStream {
		std::shared_ptr<const MipmapStream> m_underlying;

	public:
		WrappedMipmapStream(uint16_t width, uint16_t height, CompressionType type, std::shared_ptr<const MipmapStream> underlying)
			: MipmapStream(width, height, type)
			, m_underlying(std::move(underlying)) {
		}

		[[nodiscard]] uint64_t StreamSize() const override { return m_underlying->StreamSize(); }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override { return m_underlying->ReadStreamPartial(offset, buf, length); }
	};

	class MemoryBackedMipmap : public MipmapStream {
		std::vector<uint8_t> m_data;

	public:
		MemoryBackedMipmap(uint16_t width, uint16_t height, CompressionType type, std::vector<uint8_t> data)
			: MipmapStream(width, height, type)
			, m_data(std::move(data)) {
		}

		static std::shared_ptr<MemoryBackedMipmap> NewARGB8888From(const MipmapStream* stream, CompressionType type = CompressionType::ARGB_1);

		[[nodiscard]] uint64_t StreamSize() const override { return static_cast<uint32_t>(m_data.size());  }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		[[nodiscard]] auto& View() { return m_data; }
		[[nodiscard]] const auto& View() const { return m_data; }
	};

	class FileBackedReadOnlyMipmap : public MipmapStream {
		const Utils::Win32::File m_file;

	public:
		FileBackedReadOnlyMipmap(uint16_t width, uint16_t height, CompressionType type, Utils::Win32::File file)
			: MipmapStream(width, height, type)
			, m_file(std::move(file)) {
		}

		[[nodiscard]] uint64_t StreamSize() const override { return static_cast<uint32_t>(m_file.GetLength()); }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override { return m_file.Read(offset, buf, static_cast<size_t>(length)); }
	};
}
