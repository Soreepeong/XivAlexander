#ifndef _XIVRES_TEXTURE_H_
#define _XIVRES_TEXTURE_H_

#include "Internal/ByteOrder.h"

#include "Common.h"

namespace XivRes {
	enum class TextureFormat : uint32_t {
		Unknown = 0,
		L8 = 4400,
		A8 = 4401,
		A4R4G4B4 = 5184,
		A1R5G5B5 = 5185,
		A8R8G8B8 = 5200,
		X8R8G8B8 = 5201,
		R32F = 8528,
		G16R16F = 8784,
		G32R32F = 8800,
		A16B16G16R16F = 9312,
		A32B32G32R32F = 9328,
		DXT1 = 13344,
		DXT3 = 13360,
		DXT5 = 13361,
		D16 = 16704,
	};

	struct TextureHeader {
		LE<uint16_t> Unknown1;
		LE<uint16_t> HeaderSize;
		LE<TextureFormat> Type;
		LE<uint16_t> Width;
		LE<uint16_t> Height;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		char Unknown2[0xC]{};
	};

	inline size_t TextureRawDataLength(TextureFormat type, size_t width, size_t height, size_t depth, size_t mipmapIndex = 0) {
		width = (std::max<size_t>)(1, width >> mipmapIndex);
		height = (std::max<size_t>)(1, height >> mipmapIndex);
		depth = (std::max<size_t>)(1, depth >> mipmapIndex);
		switch (type) {
			case TextureFormat::L8:
			case TextureFormat::A8:
				return width * height * depth;

			case TextureFormat::A4R4G4B4:
			case TextureFormat::A1R5G5B5:
				return width * height * depth * 2;

			case TextureFormat::A8R8G8B8:
			case TextureFormat::X8R8G8B8:
			case TextureFormat::R32F:
			case TextureFormat::G16R16F:
				return width * height * depth * 4;

			case TextureFormat::A16B16G16R16F:
			case TextureFormat::G32R32F:
				return width * height * depth * 8;

			case TextureFormat::A32B32G32R32F:
				return width * height * depth * 16;

			case TextureFormat::DXT1:
				return depth * (std::max<size_t>)(1, ((width + 3) / 4)) * (std::max<size_t>)(1, ((height + 3) / 4)) * 8;

			case TextureFormat::DXT3:
			case TextureFormat::DXT5:
				return depth * (std::max<size_t>)(1, ((width + 3) / 4)) * (std::max<size_t>)(1, ((height + 3) / 4)) * 16;

			case TextureFormat::D16:
			case TextureFormat::Unknown:
			default:
				throw std::invalid_argument("Unsupported type");
		}
	}

	inline size_t TextureRawDataLength(const TextureHeader& header, size_t mipmapIndex = 0) {
		return TextureRawDataLength(header.Type, header.Width, header.Height, header.Depth, mipmapIndex);
	}
}

#endif