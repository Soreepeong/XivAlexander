#include "pch.h"
#include "Utils__Zlib.h"

std::vector<uint8_t> Utils::ZlibDecompress(const uint8_t* src, size_t length) {
	z_stream stream{};
	stream.next_in = src;
	stream.avail_in = static_cast<uInt>(length);
	if (const auto res = inflateInit(&stream); res != Z_OK)
		throw ZlibError(res);

	try {
		std::vector<uint8_t> result;
		size_t pos = 0;
		while (true) {
			result.resize(result.size() + 8192);
			stream.avail_out = static_cast<uInt>(result.size() - pos);
			stream.next_out = &result[pos];
			const auto res = inflate(&stream, Z_FINISH);
			if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
				throw ZlibError(res);
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

std::vector<uint8_t> Utils::ZlibCompress(const uint8_t* src, size_t length,
	int level, int method, int windowBits, int memLevel, int strategy) {
	z_stream stream{};
	stream.next_in = src;
	stream.avail_in = static_cast<uInt>(length);
	if (const auto res = deflateInit2(&stream, level, method, windowBits, memLevel, strategy); res != Z_OK)
		throw ZlibError(res);

	try {
		std::vector<uint8_t> result;
		size_t pos = 0;
		while (true) {
			result.resize(result.size() + 8192);
			stream.avail_out = static_cast<uInt>(result.size() - pos);
			stream.next_out = &result[pos];
			const auto res = deflate(&stream, Z_FINISH);
			if (res != Z_OK && res != Z_BUF_ERROR && res != Z_STREAM_END)
				throw ZlibError(res);
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

std::string Utils::ZlibError::DescribeReturnCode(int code) {
	switch (code) {
		case Z_OK: return "OK";
		case Z_STREAM_END: return "Stream end";
		case Z_NEED_DICT: return "Need dict";
		case Z_ERRNO: return std::generic_category().message(code);
		case Z_STREAM_ERROR: return "Stream error";
		case Z_DATA_ERROR: return "Data error";
		case Z_MEM_ERROR: return "Memory error";
		case Z_BUF_ERROR: return "Buffer error";
		case Z_VERSION_ERROR: return "Version error";
		default: return std::format("Unknown return code {}", code);
	}
}

Utils::ZlibError::ZlibError(int returnCode)
	: std::runtime_error(DescribeReturnCode(returnCode)) {
}
