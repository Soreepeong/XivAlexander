#pragma once
#include "Sqex.h"

namespace Sqex::Texture {
	enum class CompressionType : uint32_t {
		Unknown = 0,

		// https://github.com/goaaats/ffxiv-explorer-fork/blob/develop/src/main/java/com/fragmenterworks/ffxivextract/models/Texture_File.java
		
		// Grayscale
		L8_1 = 0x1130,      // 1 byte (L8) per pixel
		L8_2 = 0x1131,      // same with above

		// Full color with alpha channel
		RGBA4444 = 0x1440,  // 2 bytes (LE binary[16]: aaaaRRRRggggBBBB) per pixel
		RGBA5551 = 0x1441,  // 2 bytes (LE binary[16]: aRRRRRgggggBBBBB) per pixel
		RGBA_1 = 0x1450,    // 4 bytes (LE binary[32]: aaaaaaaaRRRRRRRRggggggggBBBBBBBB) per pixel
		RGBA_2 = 0x1451,    // same with above
		RGBAF = 0x2460,     // 8 bytes (LE half[4]: r, g, b, a)
		//                     ^ TODO: check if it's rgba or abgr

		// https://en.wikipedia.org/wiki/S3_Texture_Compression
		DXT1 = 0x3420,
		DXT3 = 0x3430,
		DXT5 = 0x3431,
	};

	struct Header {
		LE<uint16_t> Unknown1;
		LE<uint16_t> HeaderSize;
		LE<CompressionType> Type;
		LE<uint16_t> Width;
		LE<uint16_t> Height;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		char Unknown2[0xb]{};
	};
}
