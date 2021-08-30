#pragma once

#include <vector>
#include <stdexcept>

#include <zlib.h>

namespace Utils {
	std::vector<uint8_t> ZlibDecompress(const uint8_t* src, size_t length);
	std::vector<uint8_t> ZlibCompress(const uint8_t* src, size_t length, 
		int level = Z_DEFAULT_COMPRESSION,
		int method = Z_DEFLATED, 
		int windowBits = 15,
		int memLevel = 8, 
		int strategy = Z_DEFAULT_STRATEGY);

	class ZlibError : public std::runtime_error {
	public:
		static std::string DescribeReturnCode(int code);
		explicit ZlibError(int returnCode);
	};
}
