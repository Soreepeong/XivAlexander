#pragma once

#include "Internal/Dxt.h"

#include "PixelFormats.h"
#include "IStream.h"
#include "Texture.h"

namespace XivRes {
	class TextureStream;

	class MipmapStream : public DefaultAbstractStream {
	public:
		const uint16_t Width;
		const uint16_t Height;
		const uint16_t Depth;
		const TextureFormat Type;
		const std::streamsize SupposedMipmapLength;

		MipmapStream(size_t width, size_t height, size_t depths, TextureFormat type) : Width(static_cast<uint16_t>(width))
			, Height(static_cast<uint16_t>(height))
			, Depth(static_cast<uint16_t>(depths))
			, Type(type)
			, SupposedMipmapLength(static_cast<std::streamsize>(TextureRawDataLength(type, width, height, depths))) {
			if (Width != width || Height != height || Depth != depths)
				throw std::invalid_argument("dimensions can hold only uint16 ranges");
		}

		[[nodiscard]] std::streamsize StreamSize() const override {
			return SupposedMipmapLength;
		}

		std::shared_ptr<TextureStream> ToSingleTextureStream();
	};

	class WrappedMipmapStream : public MipmapStream {
		std::shared_ptr<const IStream> m_underlying;

	public:
		WrappedMipmapStream(TextureHeader header, size_t mipmapIndex, std::shared_ptr<const IStream> underlying)
			: MipmapStream(
				(std::max)(1, header.Width >> mipmapIndex),
				(std::max)(1, header.Height >> mipmapIndex),
				(std::max)(1, header.Depth >> mipmapIndex),
				header.Type)
			, m_underlying(std::move(underlying)) {}

		WrappedMipmapStream(size_t width, size_t height, size_t depths, TextureFormat type, std::shared_ptr<const IStream> underlying)
			: MipmapStream(width, height, depths, type)
			, m_underlying(std::move(underlying)) {}

		std::streamsize ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const override {
			if (offset + length > SupposedMipmapLength)
				length = SupposedMipmapLength - offset;

			const auto read = m_underlying->ReadStreamPartial(offset, buf, length);
			std::fill_n(static_cast<char*>(buf) + read, length - read, 0);
			return length;
		}
	};

	class MemoryMipmapStream : public MipmapStream {
		std::vector<uint8_t> m_data;

	public:
		MemoryMipmapStream(size_t width, size_t height, size_t depths, TextureFormat type)
			: MipmapStream(width, height, depths, type)
			, m_data(SupposedMipmapLength) {}

		MemoryMipmapStream(size_t width, size_t height, size_t depths, TextureFormat type, std::vector<uint8_t> data)
			: MipmapStream(width, height, depths, type)
			, m_data(std::move(data)) {
			m_data.resize(SupposedMipmapLength);
		}

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

		static std::shared_ptr<MemoryMipmapStream> AsARGB8888(const MipmapStream& stream) {
			const auto pixelCount = TextureRawDataLength(TextureFormat::L8, stream.Width, stream.Height, stream.Depth);
			const auto cbSource = (std::min)(static_cast<size_t>(stream.StreamSize()), TextureRawDataLength(stream.Type, stream.Width, stream.Height, stream.Depth));

			std::vector<uint8_t> result(pixelCount * sizeof RGBA8888);
			const auto rgba8888view = Internal::span_cast<LE<RGBA8888>>(result);
			uint32_t pos = 0, read = 0;
			uint8_t buf8[8192];
			switch (stream.Type) {
				case TextureFormat::L8:
				{
					if (cbSource < pixelCount)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0; i < len; ++pos, ++i)
							rgba8888view[pos] = RGBA8888(buf8[i], buf8[i], buf8[i], 255);
					}
					break;
				}

				case TextureFormat::A8:
				{
					if (cbSource < pixelCount)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0; i < len; ++pos, ++i)
							rgba8888view[pos] = RGBA8888(255, 255, 255, buf8[i]);
					}
					break;
				}

				case TextureFormat::A4R4G4B4:
				{
					if (cbSource < pixelCount * sizeof RGBA4444)
						throw std::runtime_error("Truncated data detected");
					const auto view = Internal::span_cast<LE<RGBA4444>>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBA4444; i < count; ++pos, ++i)
							rgba8888view[pos] = RGBA8888((*view[i]).R * 17, (*view[i]).G * 17, (*view[i]).B * 17, (*view[i]).A * 17);
					}
					break;
				}

				case TextureFormat::A1R5G5B5:
				{
					if (cbSource < pixelCount * sizeof RGBA5551)
						throw std::runtime_error("Truncated data detected");
					const auto view = Internal::span_cast<LE<RGBA5551>>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBA5551; i < count; ++pos, ++i)
							rgba8888view[pos] = RGBA8888((*view[i]).R * 255 / 31, (*view[i]).G * 255 / 31, (*view[i]).B * 255 / 31, (*view[i]).A * 255);
					}
					break;
				}

				case TextureFormat::A8R8G8B8:
					if (cbSource < pixelCount * sizeof RGBA8888)
						throw std::runtime_error("Truncated data detected");
					ReadStream(stream, 0, std::span(rgba8888view));
					break;

				case TextureFormat::X8R8G8B8:
					if (cbSource < pixelCount * sizeof RGBA8888)
						throw std::runtime_error("Truncated data detected");
					ReadStream(stream, 0, std::span(rgba8888view));
					for (auto& item : rgba8888view) {
						const auto native = *item;
						item = RGBA8888(native.R, native.G, native.B, 0xFF);
					}
					break;

				case TextureFormat::A16B16G16R16F:
				{
					if (cbSource < pixelCount * sizeof RGBAF16)
						throw std::runtime_error("Truncated data detected");
					ReadStream(stream, 0, std::span(rgba8888view));
					const auto view = Internal::span_cast<RGBAF16>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBAF16; i < count; ++pos, ++i) {
							rgba8888view[pos] = RGBA8888(
								static_cast<uint32_t>(std::round(255 * view[i].R)),
								static_cast<uint32_t>(std::round(255 * view[i].G)),
								static_cast<uint32_t>(std::round(255 * view[i].B)),
								static_cast<uint32_t>(std::round(255 * view[i].A))
							);
						}
					}
					break;
				}

				case TextureFormat::A32B32G32R32F:
				{
					if (cbSource < pixelCount * sizeof RGBAF32)
						throw std::runtime_error("Truncated data detected");
					ReadStream(stream, 0, std::span(rgba8888view));
					const auto view = Internal::span_cast<RGBAF32>(buf8);
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len / sizeof RGBAF32; i < count; ++pos, ++i)
							rgba8888view[pos] = RGBA8888(
								static_cast<uint32_t>(std::round(255 * view[i].R)),
								static_cast<uint32_t>(std::round(255 * view[i].G)),
								static_cast<uint32_t>(std::round(255 * view[i].B)),
								static_cast<uint32_t>(std::round(255 * view[i].A))
							);
					}
					break;
				}

				case TextureFormat::DXT1:
				{
					if (cbSource * 2 < pixelCount)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
							Internal::DecompressBlockDXT1(
								pos / 2 % stream.Width,
								pos / 2 / stream.Width * 4,
								stream.Width, &buf8[i], &rgba8888view[0]);
						}
					}
					break;
				}

				case TextureFormat::DXT3:
				{
					if (cbSource * 4 < pixelCount)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
							Internal::DecompressBlockDXT1(
								pos / 4 % stream.Width,
								pos / 4 / stream.Width * 4,
								stream.Width, &buf8[i], &rgba8888view[0]);
							for (size_t dy = 0; dy < 4; dy += 1) {
								for (size_t dx = 0; dx < 4; dx += 2) {
									const auto native1 = *rgba8888view[dy * stream.Width + dx];
									rgba8888view[dy * stream.Width + dx] = RGBA8888(native1.R, native1.G, native1.B, 17 * (buf8[i + dy * 2 + dx / 2] & 0xF));

									const auto native2 = *rgba8888view[dy * stream.Width + dx + 1];
									rgba8888view[dy * stream.Width + dx + 1] = RGBA8888(native2.R, native2.G, native2.B, 17 * (buf8[i + dy * 2 + dx / 2] >> 4));
								}
							}
						}
					}
					break;
				}

				case TextureFormat::DXT5:
				{
					if (cbSource * 4 < pixelCount)
						throw std::runtime_error("Truncated data detected");
					while (const auto len = static_cast<uint32_t>((std::min<uint64_t>)(cbSource - read, sizeof buf8))) {
						ReadStream(stream, read, buf8, len);
						read += len;
						for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
							Internal::DecompressBlockDXT5(
								pos / 4 % stream.Width,
								pos / 4 / stream.Width * 4,
								stream.Width, &buf8[i], &rgba8888view[0]);
						}
					}
					break;
				}

				case TextureFormat::Unknown:
				default:
					throw std::runtime_error("Unsupported type");
			}

			return std::make_shared<MemoryMipmapStream>(stream.Width, stream.Height, stream.Depth, TextureFormat::A8R8G8B8, std::move(result));
		}

		static std::shared_ptr<const MipmapStream> AsConstARGB8888(std::shared_ptr<const MipmapStream> stream) {
			if (stream->Type == TextureFormat::A8R8G8B8)
				return std::make_shared<WrappedMipmapStream>(stream->Width, stream->Height, stream->Depth, stream->Type, std::move(stream));

			return MemoryMipmapStream::AsARGB8888(*stream);
		}
	};
}
