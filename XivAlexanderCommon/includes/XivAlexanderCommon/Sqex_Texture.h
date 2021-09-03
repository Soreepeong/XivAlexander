#pragma once
#include "Sqex.h"

namespace Sqex::Texture {
	struct Header {
		LE<uint16_t> Unknown1;
		LE<uint16_t> HeaderSize;
		LE<uint32_t> CompressionType;
		LE<uint16_t> DecompressedWidth;
		LE<uint16_t> DecompressedHeight;
		LE<uint16_t> Depth;
		LE<uint16_t> MipmapCount;
		char Unknown2[0xb]{};
	};
}
