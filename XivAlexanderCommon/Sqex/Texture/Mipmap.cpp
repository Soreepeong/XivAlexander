#include "XivAlexanderCommon/Sqex/Texture/Mipmap.h"

// From https://github.com/Benjamin-Dobell/s3tc-dxt-decompression/blob/master/s3tc.h
#pragma warning(push, 0)
namespace Dxt {
	// uint32_t PackRGBA(): Helper method that packs RGBA channels into a single 4 byte pixel.
	//
	// uint8_t r:     red channel.
	// uint8_t g:     green channel.
	// uint8_t b:     blue channel.
	// uint8_t a:     alpha channel.

	inline uint32_t PackRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		return ((r << 24) | (g << 16) | (b << 8) | a);
	}

	// void DecompressBlockDXT1(): Decompresses one block of a DXT1 texture and stores the resulting pixels at the appropriate offset in 'image'.
	//
	// uint32_t x:                     x-coordinate of the first pixel in the block.
	// uint32_t y:                     y-coordinate of the first pixel in the block.
	// uint32_t width:                 width of the texture being decompressed.
	// uint32_t height:                height of the texture being decompressed.
	// const uint8_t *blockStorage:   pointer to the block to decompress.
	// uint32_t *image:                pointer to image where the decompressed pixel data should be stored.

	static void DecompressBlockDXT1(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, uint32_t* image) {
		uint16_t color0 = *reinterpret_cast<const uint16_t*>(blockStorage);
		uint16_t color1 = *reinterpret_cast<const uint16_t*>(blockStorage + 2);

		uint32_t temp;

		temp = (color0 >> 11) * 255 + 16;
		uint8_t r0 = (uint8_t)((temp / 32 + temp) / 32);
		temp = ((color0 & 0x07E0) >> 5) * 255 + 32;
		uint8_t g0 = (uint8_t)((temp / 64 + temp) / 64);
		temp = (color0 & 0x001F) * 255 + 16;
		uint8_t b0 = (uint8_t)((temp / 32 + temp) / 32);

		temp = (color1 >> 11) * 255 + 16;
		uint8_t r1 = (uint8_t)((temp / 32 + temp) / 32);
		temp = ((color1 & 0x07E0) >> 5) * 255 + 32;
		uint8_t g1 = (uint8_t)((temp / 64 + temp) / 64);
		temp = (color1 & 0x001F) * 255 + 16;
		uint8_t b1 = (uint8_t)((temp / 32 + temp) / 32);

		uint32_t code = *reinterpret_cast<const uint32_t*>(blockStorage + 4);

		for (int j = 0; j < 4; j++) {
			for (int i = 0; i < 4; i++) {
				uint32_t finalColor = 0;
				uint8_t positionCode = (code >> 2 * (4 * j + i)) & 0x03;

				if (color0 > color1) {
					switch (positionCode) {
						case 0:
							finalColor = PackRGBA(r0, g0, b0, 255);
							break;
						case 1:
							finalColor = PackRGBA(r1, g1, b1, 255);
							break;
						case 2:
							finalColor = PackRGBA((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255);
							break;
						case 3:
							finalColor = PackRGBA((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255);
							break;
					}
				} else {
					switch (positionCode) {
						case 0:
							finalColor = PackRGBA(r0, g0, b0, 255);
							break;
						case 1:
							finalColor = PackRGBA(r1, g1, b1, 255);
							break;
						case 2:
							finalColor = PackRGBA((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
							break;
						case 3:
							finalColor = PackRGBA(0, 0, 0, 255);
							break;
					}
				}

				if (x + i < width)
					image[(y + j) * width + (x + i)] = finalColor;
			}
		}
	}

	// void BlockDecompressImageDXT1(): Decompresses all the blocks of a DXT1 compressed texture and stores the resulting pixels in 'image'.
	//
	// uint32_t width:                 Texture width.
	// uint32_t height:                Texture height.
	// const uint8_t *blockStorage:   pointer to compressed DXT1 blocks.
	// uint32_t *image:                pointer to the image where the decompressed pixels will be stored.

	static void BlockDecompressImageDXT1(uint32_t width, uint32_t height, const uint8_t* blockStorage, uint32_t* image) {
		uint32_t blockCountX = (width + 3) / 4;
		uint32_t blockCountY = (height + 3) / 4;
		uint32_t blockWidth = (width < 4) ? width : 4;
		uint32_t blockHeight = (height < 4) ? height : 4;

		for (uint32_t j = 0; j < blockCountY; j++) {
			for (uint32_t i = 0; i < blockCountX; i++) DecompressBlockDXT1(i * 4, j * 4, width, blockStorage + i * 8, image);
			blockStorage += blockCountX * 8;
		}
	}

	// void DecompressBlockDXT5(): Decompresses one block of a DXT5 texture and stores the resulting pixels at the appropriate offset in 'image'.
	//
	// uint32_t x:                     x-coordinate of the first pixel in the block.
	// uint32_t y:                     y-coordinate of the first pixel in the block.
	// uint32_t width:                 width of the texture being decompressed.
	// uint32_t height:                height of the texture being decompressed.
	// const uint8_t *blockStorage:   pointer to the block to decompress.
	// uint32_t *image:                pointer to image where the decompressed pixel data should be stored.

	static void DecompressBlockDXT5(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, uint32_t* image) {
		uint8_t alpha0 = *reinterpret_cast<const uint8_t*>(blockStorage);
		uint8_t alpha1 = *reinterpret_cast<const uint8_t*>(blockStorage + 1);

		const uint8_t* bits = blockStorage + 2;
		uint32_t alphaCode1 = bits[2] | (bits[3] << 8) | (bits[4] << 16) | (bits[5] << 24);
		uint16_t alphaCode2 = bits[0] | (bits[1] << 8);

		uint16_t color0 = *reinterpret_cast<const uint16_t*>(blockStorage + 8);
		uint16_t color1 = *reinterpret_cast<const uint16_t*>(blockStorage + 10);

		uint32_t temp;

		temp = (color0 >> 11) * 255 + 16;
		uint8_t r0 = (uint8_t)((temp / 32 + temp) / 32);
		temp = ((color0 & 0x07E0) >> 5) * 255 + 32;
		uint8_t g0 = (uint8_t)((temp / 64 + temp) / 64);
		temp = (color0 & 0x001F) * 255 + 16;
		uint8_t b0 = (uint8_t)((temp / 32 + temp) / 32);

		temp = (color1 >> 11) * 255 + 16;
		uint8_t r1 = (uint8_t)((temp / 32 + temp) / 32);
		temp = ((color1 & 0x07E0) >> 5) * 255 + 32;
		uint8_t g1 = (uint8_t)((temp / 64 + temp) / 64);
		temp = (color1 & 0x001F) * 255 + 16;
		uint8_t b1 = (uint8_t)((temp / 32 + temp) / 32);

		uint32_t code = *reinterpret_cast<const uint32_t*>(blockStorage + 12);

		for (int j = 0; j < 4; j++) {
			for (int i = 0; i < 4; i++) {
				int alphaCodeIndex = 3 * (4 * j + i);
				int alphaCode;

				if (alphaCodeIndex <= 12) {
					alphaCode = (alphaCode2 >> alphaCodeIndex) & 0x07;
				} else if (alphaCodeIndex == 15) {
					alphaCode = (alphaCode2 >> 15) | ((alphaCode1 << 1) & 0x06);
				} else // alphaCodeIndex >= 18 && alphaCodeIndex <= 45
				{
					alphaCode = (alphaCode1 >> (alphaCodeIndex - 16)) & 0x07;
				}

				uint8_t finalAlpha;
				if (alphaCode == 0) {
					finalAlpha = alpha0;
				} else if (alphaCode == 1) {
					finalAlpha = alpha1;
				} else {
					if (alpha0 > alpha1) {
						finalAlpha = ((8 - alphaCode) * alpha0 + (alphaCode - 1) * alpha1) / 7;
					} else {
						if (alphaCode == 6)
							finalAlpha = 0;
						else if (alphaCode == 7)
							finalAlpha = 255;
						else
							finalAlpha = ((6 - alphaCode) * alpha0 + (alphaCode - 1) * alpha1) / 5;
					}
				}

				uint8_t colorCode = (code >> 2 * (4 * j + i)) & 0x03;

				uint32_t finalColor;
				switch (colorCode) {
					case 0:
						finalColor = PackRGBA(r0, g0, b0, finalAlpha);
						break;
					case 1:
						finalColor = PackRGBA(r1, g1, b1, finalAlpha);
						break;
					case 2:
						finalColor = PackRGBA((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, finalAlpha);
						break;
					case 3:
						finalColor = PackRGBA((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, finalAlpha);
						break;
				}

				if (x + i < width)
					image[(y + j) * width + (x + i)] = finalColor;
			}
		}
	}

	// void BlockDecompressImageDXT5(): Decompresses all the blocks of a DXT5 compressed texture and stores the resulting pixels in 'image'.
	//
	// uint32_t width:                 Texture width.
	// uint32_t height:                Texture height.
	// const uint8_t *blockStorage:   pointer to compressed DXT5 blocks.
	// uint32_t *image:                pointer to the image where the decompressed pixels will be stored.

	static void BlockDecompressImageDXT5(uint32_t width, uint32_t height, const uint8_t* blockStorage, uint32_t* image) {
		uint32_t blockCountX = (width + 3) / 4;
		uint32_t blockCountY = (height + 3) / 4;
		uint32_t blockWidth = (width < 4) ? width : 4;
		uint32_t blockHeight = (height < 4) ? height : 4;

		for (uint32_t j = 0; j < blockCountY; j++) {
			for (uint32_t i = 0; i < blockCountX; i++) DecompressBlockDXT5(i * 4, j * 4, width, blockStorage + i * 16, image);
			blockStorage += blockCountX * 16;
		}
	}
}
#pragma warning(pop)

Sqex::Texture::MipmapStream::MipmapStream(size_t width, size_t height, size_t layers, Format type)
	: Width(static_cast<uint16_t>(width))
	, Height(static_cast<uint16_t>(height))
	, Depth(static_cast<uint16_t>(layers))
	, Type(type) {
	if (Width != width || Height != height || Depth != layers)
		throw std::invalid_argument("dimensions can hold only uint16 ranges");
}

std::shared_ptr<const Sqex::Texture::MipmapStream> Sqex::Texture::MipmapStream::ViewARGB8888(Format type) const {
	if (type != Format::A8R8G8B8 && type != Format::X8R8G8B8 && type != Format::Unknown)
		throw std::invalid_argument("invalid argb8888 compression type");

	if (Type == Format::A8R8G8B8 || Type == Format::X8R8G8B8) {
		auto res = std::static_pointer_cast<const MipmapStream>(shared_from_this());
		if (Type == type || type == Format::Unknown)
			return res;
		else
			return std::make_shared<WrappedMipmapStream>(Width, Height, Depth, type, std::move(res));
	}

	if (type == Format::Unknown)
		return MemoryBackedMipmap::NewARGB8888From(this, Format::A8R8G8B8);
	else
		return MemoryBackedMipmap::NewARGB8888From(this, type);
}

std::shared_ptr<Sqex::Texture::MemoryBackedMipmap> Sqex::Texture::MemoryBackedMipmap::NewARGB8888From(const MipmapStream* stream, Format type) {
	if (type != Format::A8R8G8B8 && type != Format::X8R8G8B8)
		throw std::invalid_argument("invalid argb8888 compression type");

	const auto width = stream->Width;
	const auto height = stream->Height;
	const auto pixelCount = static_cast<size_t>(width) * height;
	const auto cbSource = static_cast<size_t>(stream->StreamSize());

	std::vector<uint8_t> result(pixelCount * sizeof RGBA8888);
	const auto rgba8888view = span_cast<RGBA8888>(result);
	uint32_t pos = 0, read = 0;
	uint8_t buf8[8192];
	switch (stream->Type) {
		case Format::L8:
		case Format::A8:
		{
			if (cbSource < pixelCount)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0; i < len; ++pos, ++i) {
					rgba8888view[pos].Value = buf8[i] * 0x10101UL | 0xFF000000UL;
					// result[pos].R = result[pos].G = result[pos].B = result[pos].A = buf8[i];
				}
			}
			break;
		}

		case Format::A4R4G4B4:
		{
			if (cbSource < pixelCount * sizeof RGBA4444)
				throw std::runtime_error("Truncated data detected");
			const auto view = span_cast<RGBA4444>(buf8);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBA4444; i < count; ++pos, ++i)
					rgba8888view[pos].SetFrom(view[i].R * 17, view[i].G * 17, view[i].B * 17, view[i].A * 17);
			}
			break;
		}

		case Format::A1R5G5B5:
		{
			if (cbSource < pixelCount * sizeof RGBA5551)
				throw std::runtime_error("Truncated data detected");
			const auto view = span_cast<RGBA5551>(buf8);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBA5551; i < count; ++pos, ++i)
					rgba8888view[pos].SetFrom(view[i].R * 255 / 31, view[i].G * 255 / 31, view[i].B * 255 / 31, view[i].A * 255);
			}
			break;
		}

		case Format::A8R8G8B8:
		case Format::X8R8G8B8:
			if (cbSource < pixelCount * sizeof RGBA8888)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			break;

		case Format::A16B16G16R16F:
		{
			if (cbSource < pixelCount * sizeof RGBAHHHH)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			const auto view = span_cast<RGBAHHHH>(buf8);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBAHHHH; i < count; ++pos, ++i)
					rgba8888view[pos].SetFromF(view[i]);
			}
			break;
		}

		case Format::A32B32G32R32F:
		{
			if (cbSource < pixelCount * sizeof RGBAFFFF)
				throw std::runtime_error("Truncated data detected");
			stream->ReadStream(0, std::span(rgba8888view));
			const auto view = span_cast<RGBAFFFF>(buf8);
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len / sizeof RGBAFFFF; i < count; ++pos, ++i)
					rgba8888view[pos].SetFromF(view[i]);
			}
			break;
		}

		case Format::DXT1:
		{
			if (cbSource < pixelCount * 8)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 8, pos += 8) {
					Dxt::DecompressBlockDXT1(
						pos / 2 % width,
						pos / 2 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
				}
			}
			break;
		}

		case Format::DXT3:
		{
			if (cbSource < pixelCount * 16)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					Dxt::DecompressBlockDXT1(
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

		case Format::DXT5:
		{
			if (cbSource < pixelCount * 16)
				throw std::runtime_error("Truncated data detected");
			while (const auto len = static_cast<uint32_t>(std::min<uint64_t>(cbSource - read, sizeof buf8))) {
				stream->ReadStream(read, buf8, len);
				read += len;
				for (size_t i = 0, count = len; i < count; i += 16, pos += 16) {
					Dxt::DecompressBlockDXT5(
						pos / 4 % width,
						pos / 4 / width * 4,
						width, &buf8[i], reinterpret_cast<uint32_t*>(&rgba8888view[0]));
				}
			}
			break;
		}

		case Format::Unknown:
		default:
			throw std::runtime_error("Unsupported type");
	}

	return std::make_shared<MemoryBackedMipmap>(stream->Width, stream->Height, stream->Depth, type, std::move(result));
}

std::streamsize Sqex::Texture::MemoryBackedMipmap::ReadStreamPartial(std::streamoff offset, void* buf, std::streamsize length) const {
	const auto available = std::min(static_cast<std::streamsize>(m_data.size() - offset), length);
	std::copy_n(&m_data[static_cast<size_t>(offset)], available, static_cast<char*>(buf));
	return available;
}
