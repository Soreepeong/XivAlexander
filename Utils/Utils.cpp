#include "pch.h"
#include "include/Utils.h"
#include "include/myzlib.h"

std::wstring Utils::FromOem(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
	std::wstring u16(length, 0);
	MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), const_cast<LPWSTR>(u16.c_str()), static_cast<int>(u16.size()));
	return u16;
}

std::wstring Utils::FromUtf8(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
	std::wstring u16(length, 0);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), const_cast<LPWSTR>(u16.c_str()), static_cast<int>(u16.size()));
	return u16;
}

std::string Utils::ToUtf8(const std::wstring& u16) {
	const size_t length = WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), static_cast<int>(u16.size()), nullptr, 0, nullptr, nullptr);
	std::string u8(length, 0);
	WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), static_cast<int>(u16.size()), const_cast<LPSTR>(u8.c_str()), static_cast<int>(u8.size()), nullptr, nullptr);
	return u8;
}

uint64_t Utils::GetEpoch() {
	union {
		FILETIME ft;
		LARGE_INTEGER li;
	};
	GetSystemTimePreciseAsFileTime(&ft);
	return (li.QuadPart - 116444736000000000ULL) / 10 / 1000;
}

SYSTEMTIME Utils::EpochToLocalSystemTime(uint64_t epochMilliseconds) {
	union {
		FILETIME ft;
		LARGE_INTEGER li;
	};
	FILETIME lft;
	SYSTEMTIME st;

	li.QuadPart = epochMilliseconds * 10 * 1000ULL + 116444736000000000ULL;
	FileTimeToLocalFileTime(&ft, &lft);
	FileTimeToSystemTime(&lft, &st);
	return st;
}

uint64_t Utils::GetHighPerformanceCounter(int32_t multiplier) {
	LARGE_INTEGER time, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&time);
	return time.QuadPart * multiplier / freq.QuadPart;
}

std::string Utils::ToUtf8(const std::string& in) {
	return ToUtf8(FromOem(in));
}

std::string Utils::FormatWindowsErrorMessage(unsigned int errorCode) {
	if (errorCode == -1)
		errorCode = GetLastError();
	std::string res;
	LPTSTR errorText = nullptr;
	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPTSTR>(&errorText), // output 
		0, // minimum size for output buffer
		nullptr); // arguments - see note 

	if (nullptr != errorText) {
		OutputDebugString(FormatString(TEXT("Windows Error: %s\n"), errorText).c_str());
		res = ToUtf8(errorText);
		LocalFree(errorText);
	}
	return res;
}

static void sockaddr_cmp_helper(int x, int y) {
	if (x < y)
		throw (-1);
	else if (x > y)
		throw 1;
}
static void sockaddr_cmp_helper(int x) {
	if (x)
		throw x;
}

int Utils::sockaddr_cmp(const void* x, const void* y) {
	const auto family1 = reinterpret_cast<const sockaddr*>(x)->sa_family;
	const auto family2 = reinterpret_cast<const sockaddr*>(y)->sa_family;
	try {
		sockaddr_cmp_helper(family1, family2);
		if (family1 == AF_INET) {
			const auto addr1 = reinterpret_cast<const sockaddr_in*>(x);
			const auto addr2 = reinterpret_cast<const sockaddr_in*>(y);
			sockaddr_cmp_helper(ntohl(addr1->sin_addr.s_addr), ntohl(addr2->sin_addr.s_addr));
			sockaddr_cmp_helper(ntohs(addr1->sin_port), ntohs(addr2->sin_port));
		} else if (family1 == AF_INET6) {
			const auto addr1 = reinterpret_cast<const sockaddr_in6*>(x);
			const auto addr2 = reinterpret_cast<const sockaddr_in6*>(y);
			sockaddr_cmp_helper(memcmp(addr1->sin6_addr.s6_addr, addr2->sin6_addr.s6_addr, sizeof addr1->sin6_addr.s6_addr));
			sockaddr_cmp_helper(ntohs(addr1->sin6_port), ntohs(addr2->sin6_port));
			sockaddr_cmp_helper(addr1->sin6_flowinfo, addr2->sin6_flowinfo);
			sockaddr_cmp_helper(addr1->sin6_scope_id, addr2->sin6_scope_id);
		}
	} catch (int r) {
		return r;
	}
	return 0;
}

std::vector<std::string> Utils::StringSplit(const std::string& str, const std::string& delimiter) {
	std::vector<std::string> result;
	if (delimiter.empty()){
		for (size_t i = 0; i < str.size(); ++i)
			result.push_back(str.substr(i, 1));
	} else {
		size_t previousOffset = 0, offset;
		while ((offset = str.find(delimiter, previousOffset)) != std::string::npos) {
			result.push_back(str.substr(previousOffset, offset - previousOffset));
			previousOffset = offset + delimiter.length();
		}
		result.push_back(str.substr(previousOffset));
	}
	return result;
}

std::string Utils::StringTrim(const std::string& str, bool leftTrim, bool rightTrim) {
	size_t left = 0, right = str.length() - 1;
	if (leftTrim)
		while (left < str.length() && std::isspace(str[left]))
			left++;
	if (rightTrim)
		while (right != SIZE_MAX && std::isspace(str[right]))
			right--;
	return str.substr(left, right + 1 - left);
}

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
