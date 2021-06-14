#include "pch.h"
#include "Utils__Zlib.h"

std::vector<uint8_t> Utils::ZlibDecompress(const uint8_t* src, size_t length) {
	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	stream.next_in = src;
	stream.avail_in = static_cast<uInt>(length);
	if (inflateInit(&stream) != Z_OK)
		throw std::exception();

	try {
		std::vector<uint8_t> result;
		size_t pos = 0;
		while (true) {
			result.resize(result.size() + 8192);
			stream.avail_out = static_cast<uInt>(result.size() - pos);
			stream.next_out = &result[pos];
			const auto res = inflate(&stream, Z_FINISH);
			if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
				throw std::exception();
			pos = result.size() - stream.avail_out;
			if (res == Z_STREAM_END)
				break;
		}
		result.resize(pos);
		inflateEnd(&stream);
		return result;
	} catch (...) {
		inflateEnd(&stream);
		throw;
	}
}

std::vector<uint8_t> Utils::ZlibCompress(const uint8_t* src, size_t length) {
	z_stream stream;
	memset(&stream, 0, sizeof(stream));
	stream.next_in = src;
	stream.avail_in = static_cast<uInt>(length);
	if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
		throw std::exception();

	try {
		std::vector<uint8_t> result;
		size_t pos = 0;
		while (true) {
			result.resize(result.size() + 8192);
			stream.avail_out = static_cast<uInt>(result.size() - pos);
			stream.next_out = &result[pos];
			const auto res = deflate(&stream, Z_FINISH);
			if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
				throw std::exception();
			pos = result.size() - stream.avail_out;
			if (res == Z_STREAM_END)
				break;
		}
		result.resize(pos);
		deflateEnd(&stream);
		return result;
	} catch (...) {
		deflateEnd(&stream);
		throw;
	}
}
