#include "pch.h"
#include "XaStrings.h"

#include "Utils_Win32.h"

std::wstring Utils::FromUtf8(std::string_view str, UINT codePage) {
	const size_t length = MultiByteToWideChar(codePage, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring wstr(length, 0);
	MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, str.data(), static_cast<int>(str.size()), const_cast<LPWSTR>(wstr.data()), static_cast<int>(wstr.size()));
	return wstr;
}

std::string Utils::ToUtf8(std::wstring_view wstr, UINT codePage) {
	const size_t length = WideCharToMultiByte(codePage, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string str(length, 0);
	WideCharToMultiByte(codePage, 0, wstr.data(), static_cast<int>(wstr.size()), const_cast<LPSTR>(str.c_str()), static_cast<int>(str.size()), nullptr, nullptr);
	return str;
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
