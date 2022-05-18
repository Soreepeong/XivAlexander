#pragma once

#include "Internal/ByteOrder.h"

namespace XivRes::Model {
	struct Header {
		LE<uint32_t> Version;
		LE<uint32_t> StackSize;
		LE<uint32_t> RuntimeSize;
		LE<uint16_t> VertexDeclarationCount;
		LE<uint16_t> MaterialCount;

		LE<uint32_t> VertexOffset[3];
		LE<uint32_t> IndexOffset[3];
		LE<uint32_t> VertexSize[3];
		LE<uint32_t> IndexSize[3];

		LE<uint8_t> LodCount;
		LE<uint8_t> EnableIndexBufferStreaming;
		LE<uint8_t> EnableEdgeGeometry;
		LE<uint8_t> Padding;
	};
}
