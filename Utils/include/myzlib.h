#include <vector>

namespace Utils {
	std::vector<uint8_t> ZlibDecompress(const uint8_t* src, size_t length);
	std::vector<uint8_t> ZlibCompress(const uint8_t* src, size_t length);
}
