#pragma once

#include "internal/Dxt.h"

#include "PixelFormats.h"
#include "RandomAccessStream.h"
#include "Texture.h"

namespace XivRes {
	class MipmapStream : public RandomAccessStream {
	public:
		const uint16_t Width;
		const uint16_t Height;
		const uint16_t Depth;
		const TextureFormat Type;

		MipmapStream(size_t width, size_t height, size_t layers, TextureFormat type) : Width(static_cast<uint16_t>(width))
			, Height(static_cast<uint16_t>(height))
			, Depth(static_cast<uint16_t>(layers))
			, Type(type) {
			if (Width != width || Height != height || Depth != layers)
				throw std::invalid_argument("dimensions can hold only uint16 ranges");
		}
	};

	class WrappedMipmapStream : public MipmapStream {
		std::shared_ptr<const RandomAccessStream> m_underlying;

	public:
		WrappedMipmapStream(TextureHeader header, size_t mipmapIndex, std::shared_ptr<const RandomAccessStream> underlying)
			: MipmapStream(
				(std::max)(1, header.Width >> mipmapIndex),
				(std::max)(1, header.Height >> mipmapIndex),
				(std::max)(1, header.Depth >> mipmapIndex),
				header.Type)
			, m_underlying(std::move(underlying)) {
		}

		WrappedMipmapStream(size_t width, size_t height, size_t layers, TextureFormat type, std::shared_ptr<const RandomAccessStream> underlying)
			: MipmapStream(width, height, layers, type)
			, m_underlying(std::move(underlying)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return m_underlying->StreamSize();
		}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			return m_underlying->ReadStreamPartial(offset, buf, length);
		}
	};

	class MemoryBackedMipmap : public MipmapStream {
		std::vector<uint8_t> m_data;

	public:
		MemoryBackedMipmap(size_t width, size_t height, size_t layers, TextureFormat type)
			: MipmapStream(width, height, layers, type)
			, m_data(TextureRawDataLength(type, width, layers, height)) {
		}

		MemoryBackedMipmap(size_t width, size_t height, size_t layers, TextureFormat type, std::vector<uint8_t> data)
			: MipmapStream(width, height, layers, type)
			, m_data(std::move(data)) {
		}

		[[nodiscard]] std::streamsize StreamSize() const override { return static_cast<uint32_t>(m_data.size()); }
		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			const auto available = (std::min)(static_cast<std::streamsize>(m_data.size() - offset), length);
			std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<char*>(buf));
			return available;
		}

		template<typename T>
		[[nodiscard]] auto View() {
			return Internal::span_cast<T>(m_data);
		}

		template<typename T>
		[[nodiscard]] auto View() const {
			return Internal::span_cast<const T>(m_data);
		}

		static std::shared_ptr<MemoryBackedMipmap> AsARGB8888(const MipmapStream* stream) {
			const auto width = stream->Width;
			const auto height = stream->Height;
			const auto pixelCount = static_cast<size_t>(width) * height;
			const auto cbSource = static_cast<size_t>(stream->StreamSize());

			std::vector<uint8_t> result(pixelCount * sizeof RGBA8888);
			const auto rgba8888view = Internal::span_cast<RGBA8888>(result);
			uint32_t pos = 0, read = 0;
			uint8_t buf8[8192];
			switch (stream->Type) {
				case TextureFormat::L8:
				case TextureFormat::A8:
				{
					if (cbSource < pixelCount)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0; i < len; ++pos, ++i) {
							rgba8888view[pos].Value = buf8[i] * 0x10101UL | 0xFF000000UL;
							// result[pos].R = result[pos].G = result[pos].B = result[pos].A = buf8[i];
						}
					}
					break;
				}

				case TextureFormat::A4R4G4B4:
				{
					if (cbSource < pixelCount * sizeof RGBA4444)
						throw std::runtime_error("Truncated data detected");
					const auto view = Internal::span_cast<RGBA4444>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBA4444; i < count; ++pos, ++i)
							rgba8888view[pos].SetFrom(view[i].R * 17, view[i].G * 17, view[i].B * 17, view[i].A * 17);
					}
					break;
				}

				case TextureFormat::A1R5G5B5:
				{
					if (cbSource < pixelCount * sizeof RGBA5551)
						throw std::runtime_error("Truncated data detected");
					const auto view = Internal::span_cast<RGBA5551>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBA5551; i < count; ++pos, ++i)
							rgba8888view[pos].SetFrom(view[i].R * 255 / 31, view[i].G * 255 / 31, view[i].B * 255 / 31, view[i].A * 255);
					}
					break;
				}

				case TextureFormat::A8R8G8B8:
					if (cbSource < pixelCount * sizeof RGBA8888)
						throw std::runtime_error("Truncated data detected");
					stream->ReadStream(0, std::span(rgba8888view));
					break;

				case TextureFormat::X8R8G8B8:
					if (cbSource < pixelCount * sizeof RGBA8888)
						throw std::runtime_error("Truncated data detected");
					stream->ReadStream(0, std::span(rgba8888view));
					for (auto& item : rgba8888view)
						item.A = 0xFF;
					break;

				case TextureFormat::A16B16G16R16F:
				{
					if (cbSource < pixelCount * sizeof RGBAHHHH)
						throw std::runtime_error("Truncated data detected");
					stream->ReadStream(0, std::span(rgba8888view));
					const auto view = Internal::span_cast<RGBAHHHH>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBAHHHH; i < count; ++pos, ++i)
							rgba8888view[pos].SetFromF(view[i]);
					}
					break;
				}

				case TextureFormat::A32B32G32R32F:
				{
					if (cbSource < pixelCount * sizeof RGBAFFFF)
						throw std::runtime_error("Truncated data detected");
					stream->ReadStream(0, std::span(rgba8888view));
					const auto view = Internal::span_cast<RGBAFFFF>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBAFFFF; i < count; ++pos, ++i)
							rgba8888view[pos].SetFromF(view[i]);
					}
					break;
				}

				case TextureFormat::DXT1:
				{
					if (cbSource < pixelCount * 8)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
							Internal::DecompressBlockDXT1(
								pos / 2 % width,
								pos / 2 / width * 4,
								width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
						}
					}
					break;
				}

				case TextureFormat::DXT3:
				{
					if (cbSource < pixelCount * 16)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
							Internal::DecompressBlockDXT1(
								pos / 4 % width,
								pos / 4 / width * 4,
								width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
							for (size_t dy = 0; dy < 4; dy += 1) {
								for (size_t dx = 0; dx < 4; dx += 2) {
									rgba8888view[dy * width + dx].A = 17 * (buf8[i + dy * 2 + dx / 2] & 0xF);
									rgba8888view[dy * width + dx + 1].A = 17 * (buf8[i + dy * 2 + dx / 2] >> 4);
								}
							}
						}
					}
					break;
				}

				case TextureFormat::DXT5:
				{
					if (cbSource < pixelCount * 16)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						stream->ReadStream(read, buf8, len);
						read += len;
						for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
							Internal::DecompressBlockDXT5(
								pos / 4 % width,
								pos / 4 / width * 4,
								width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
						}
					}
					break;
				}

				case TextureFormat::Unknown:
				default:
					throw std::runtime_error("Unsupported type");
			}

			return std::make_shared<MemoryBackedMipmap>(stream->Width, stream->Height, stream->Depth, TextureFormat::A8R8G8B8, std::move(result));
		}

		static std::shared_ptr<const MipmapStream> AsConstARGB8888(std::shared_ptr<const MipmapStream> stream) {
			if (stream->Type == TextureFormat::A8R8G8B8)
				return std::make_shared<WrappedMipmapStream>(stream->Width, stream->Height, stream->Depth, stream->Type, std::move(stream));

			return MemoryBackedMipmap::AsARGB8888(stream.get());
		}
	};
}
