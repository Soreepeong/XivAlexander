#pragma once

#include "Sqex_Texture.h"

namespace Sqex::Texture {
	class MipmapStream : public RandomAccessStream {
		const uint16_t m_width;
		const uint16_t m_height;
		const uint16_t m_layers;
		const Format m_type;

	public:
		MipmapStream(uint16_t width, uint16_t height, uint16_t layers, Format type)
			: m_width(width)
			, m_height(height)
			, m_layers(layers)
			, m_type(type) {
		}

		[[nodiscard]] auto Width() const { return m_width; }
		[[nodiscard]] auto Height() const { return m_height; }
		[[nodiscard]] auto Layers() const { return m_layers; }
		[[nodiscard]] auto Type() const { return m_type; }

		std::shared_ptr<const MipmapStream> ViewARGB8888(Format type = Format::Unknown) const;

		static std::shared_ptr<MipmapStream> FromTexture(std::shared_ptr<RandomAccessStream> stream, size_t mipmapIndex);

		void Show(std::string title = std::string("Preview")) const;
	};

	class WrappedMipmapStream : public MipmapStream {
		std::shared_ptr<const RandomAccessStream> m_underlying;

	public:
		WrappedMipmapStream(uint16_t width, uint16_t height, uint16_t layers, Format type, std::shared_ptr<const RandomAccessStream> underlying)
			: MipmapStream(width, height, layers, type)
			, m_underlying(std::move(underlying)) {
		}

		[[nodiscard]] uint64_t StreamSize() const override { return m_underlying->StreamSize(); }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override { return m_underlying->ReadStreamPartial(offset, buf, length); }
	};

	class MemoryBackedMipmap : public MipmapStream {
		std::vector<uint8_t> m_data;

	public:
		MemoryBackedMipmap(uint16_t width, uint16_t height, uint16_t layers, Format type)
			: MipmapStream(width, height, layers, type)
			, m_data(RawDataLength(type, width, layers, height)) {
		}

		MemoryBackedMipmap(uint16_t width, uint16_t height, uint16_t layers, Format type, std::vector<uint8_t> data)
			: MipmapStream(width, height, layers, type)
			, m_data(std::move(data)) {
		}

		static std::shared_ptr<MemoryBackedMipmap> NewARGB8888From(const MipmapStream* stream, Format type = Format::A8R8G8B8);

		[[nodiscard]] uint64_t StreamSize() const override { return static_cast<uint32_t>(m_data.size());  }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override;

		template<typename T>
		[[nodiscard]] auto View() {
			return std::span(reinterpret_cast<T*>(&m_data[0]), m_data.size() / sizeof T);
		}
		template<typename T>
		[[nodiscard]] auto View() const {
			return std::span(reinterpret_cast<T*>(&m_data[0]), m_data.size() / sizeof T);
		}
	};

	class FileBackedReadOnlyMipmap : public MipmapStream {
		const Win32::Handle m_file;

	public:
		FileBackedReadOnlyMipmap(uint16_t width, uint16_t height, uint16_t layers, Format type, Win32::Handle file)
			: MipmapStream(width, height, layers, type)
			, m_file(std::move(file)) {
		}

		[[nodiscard]] uint64_t StreamSize() const override { return static_cast<uint32_t>(m_file.GetFileSize()); }
		uint64_t ReadStreamPartial(uint64_t offset, void* buf, uint64_t length) const override { return m_file.Read(offset, buf, static_cast<size_t>(length)); }
	};
}
