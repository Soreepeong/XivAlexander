#include "pch.h"
#include "Utils/StringUtils.h"

std::wstring Utils::FromAnsi(std::string_view str) {
	if (str.empty())
		return {};
	std::wstring wstr(MultiByteToWideChar(CP_ACP, 0, str.data(), static_cast<int>(str.size()), nullptr, 0), 0);
	wstr.resize(MultiByteToWideChar(CP_ACP, 0, str.data(), static_cast<int>(str.size()), wstr.data(), static_cast<int>(wstr.size())));
	return wstr;
}

std::string Utils::ToAnsi(std::wstring_view wstr) {
	if (wstr.empty())
		return {};
	std::string str(WideCharToMultiByte(CP_ACP, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr), 0);
	str.resize(WideCharToMultiByte(CP_ACP, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), static_cast<int>(str.size()), nullptr, nullptr));
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
