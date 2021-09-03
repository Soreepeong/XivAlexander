// From https://github.com/Benjamin-Dobell/s3tc-dxt-decompression/blob/master/s3tc.h

#pragma once

namespace Utils {
	void DecompressBlockDXT1(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, uint32_t* image);
	void BlockDecompressImageDXT1(uint32_t width, uint32_t height, const uint8_t* blockStorage, uint32_t* image);
	void DecompressBlockDXT5(uint32_t x, uint32_t y, uint32_t width, const uint8_t* blockStorage, uint32_t* image);
	void BlockDecompressImageDXT5(uint32_t width, uint32_t height, const uint8_t* blockStorage, uint32_t* image);
}
