#pragma once

#include <vector>
#include <stdexcept>

namespace Utils {
	std::vector<uint8_t> ZlibDecompress(const uint8_t* src, size_t length);
	std::vector<uint8_t> ZlibCompress(const uint8_t* src, size_t length);

	class ZlibError : public std::runtime_error {
	public:
		static std::string DescribeReturnCode(int code);
		explicit ZlibError(int returnCode);
	};
}
