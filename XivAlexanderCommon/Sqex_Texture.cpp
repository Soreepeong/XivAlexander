#include "pch.h"
#include "Sqex_Texture.h"

size_t Sqex::Texture::RawDataLength(CompressionType type, size_t width, size_t height) {
	switch (type) {
		case CompressionType::L8_1:
		case CompressionType::L8_2:
			return width * height;

		case CompressionType::RGBA4444:
			return width * height * sizeof RGBA4444;

		case CompressionType::RGBA5551:
			return width * height * sizeof RGBA5551;

		case CompressionType::ARGB_1:
		case CompressionType::ARGB_2:
			return width * height * sizeof RGBA8888;

		case CompressionType::RGBAF:
			return width * height * sizeof RGBAHHHH;

		case CompressionType::DXT1:
			return width * height * 8;

		case CompressionType::DXT3:
		case CompressionType::DXT5:
			return width * height * 16;

		case CompressionType::Unknown:
		default:
			throw std::invalid_argument("Unsupported type");
	}
}
