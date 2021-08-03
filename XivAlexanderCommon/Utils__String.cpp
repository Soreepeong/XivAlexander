#include "pch.h"
#include "Utils__String.h"
#include "Utils_Win32.h"

std::wstring Utils::FromUtf8(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
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

std::wstring Utils::FromOem(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_OEMCP, 0, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
	std::wstring u16(length, 0);
	MultiByteToWideChar(CP_OEMCP, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), const_cast<LPWSTR>(u16.c_str()), static_cast<int>(u16.size()));
	return u16;
}

std::string Utils::ToString(const in_addr& ia) {
	char s[INET_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET, &ia, s, sizeof s);
	return s;
}

std::string Utils::ToString(const sockaddr_in& sa) {
	if (sa.sin_family != AF_INET)
		return std::format("sockaddr_in?(AF_INET={})", sa.sin_family);

	char s[INET_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET, &sa.sin_addr, s, sizeof s);
	return std::format("{}:{}", s, ntohs(sa.sin_port));
}

std::string Utils::ToString(const sockaddr_in6& sa) {
	if (sa.sin6_family != AF_INET6)
		return std::format("sockaddr_in6?(AF_INET={})", sa.sin6_family);

	char s[INET6_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET6, &sa.sin6_addr, s, sizeof s);
	return std::format("{}:{}", s, ntohs(sa.sin6_port));
}

std::string Utils::ToString(const sockaddr& sa) {
	if (sa.sa_family == AF_INET)
		return ToString(*reinterpret_cast<const sockaddr_in*>(&sa));
	if (sa.sa_family == AF_INET6)
		return ToString(*reinterpret_cast<const sockaddr_in6*>(&sa));
	return std::format("sockaddr(AF_INET={})", sa.sa_family);
}

std::string Utils::ToString(const sockaddr_storage& sa) {
	return ToString(*reinterpret_cast<const sockaddr*>(&sa));
}

std::vector<std::string> Utils::StringSplit(const std::string& str, const std::string& delimiter) {
	std::vector<std::string> result;
	if (delimiter.empty()) {
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
		while (left < str.length() && std::isspace(static_cast<uint8_t>(str[left])))
			left++;
	if (rightTrim)
		while (right != SIZE_MAX && std::isspace(static_cast<uint8_t>(str[right])))
			right--;
	return str.substr(left, right + 1 - left);
}

std::string Utils::StringReplaceAll(const std::string& source, const std::string& from, const std::string& to) {
	std::string s;
	s.reserve(source.length());

	size_t last = 0;
	size_t pos;

	while (std::string::npos != (pos = source.find(from, last))) {
		s.append(&source[last], &source[pos]);
		s += to;
		last = pos + from.length();
	}

	s += source.substr(last);
	return s;
}
