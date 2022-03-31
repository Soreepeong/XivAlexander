#pragma once
#include "XivAlexanderCommon/Sqex/Texture.h"

namespace Sqex::Texture {
	class MipmapStream : public RandomAccessStream {
	public:
		const uint16_t Width;
		const uint16_t Height;
		const uint16_t Depth;
		const Format Type;

		MipmapStream(size_t width, size_t height, size_t layers, Format type);

		std::shared_ptr<const MipmapStream> ViewARGB8888(Format type = Format::Unknown) const;
	};

	class WrappedMipmapStream : public MipmapStream {
		std::shared_ptr<const RandomAccessStream> m_underlying;

	public:
		WrappedMipmapStream(Header header, size_t mipmapIndex, std::shared_ptr<const RandomAccessStream> underlying)
			: MipmapStream(
				std::max(1, header.Width >> mipmapIndex),
				std::max(1, header.Height >> mipmapIndex),
				std::max(1, header.Depth >> mipmapIndex),
				header.Type)
			, m_underlying(std::move(underlying)) {
		}

		WrappedMipmapStream(size_t width, size_t height, size_t layers, Format type, std::shared_ptr<const RandomAccessStream> underlying)
			: MipmapStream(width, height, layers, type)
			, m_underlying(std::move(underlying)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override { return m_underlying->StreamSize(); }
		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override { return m_underlying->ReadStreamPartial(offset, buf, length); }
	};

	class MemoryBackedMipmap : public MipmapStream {
		std::vector<uint8_t> m_data;

	public:
		MemoryBackedMipmap(size_t width, size_t height, size_t layers, Format type)
			: MipmapStream(width, height, layers, type)
			, m_data(RawDataLength(type, width, layers, height)) {
		}

		MemoryBackedMipmap(size_t width, size_t height, size_t layers, Format type, std::vector<uint8_t> data)
			: MipmapStream(width, height, layers, type)
			, m_data(std::move(data)) {
		}

		static std::shared_ptr<MemoryBackedMipmap> NewARGB8888From(const MipmapStream* stream, Format type = Format::A8R8G8B8);

		[[nodiscard]] std::streamsize StreamSize() const override { return static_cast<uint32_t>(m_data.size()); }
		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override;

		template<typename T>
		[[nodiscard]] auto View() {
			return span_cast<T>(m_data);
		}

		template<typename T>
		[[nodiscard]] auto View() const {
			return span_cast<const T>(m_data);
		}
	};
}
