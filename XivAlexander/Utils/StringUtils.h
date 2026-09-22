#pragma once

#include <filesystem>
#include <string>

#include <xivres/util.unicode.h>

namespace Utils {
	std::wstring FromAnsi(std::string_view str);
	std::string ToAnsi(std::wstring_view str);

	std::string ToString(const struct in_addr& ia);
	std::string ToString(const struct sockaddr_in& sa);
	std::string ToString(const struct sockaddr_in6& sa);
	std::string ToString(const struct sockaddr& sa);
	std::string ToString(const struct sockaddr_storage& sa);
}

template<>
struct std::formatter<std::filesystem::path, wchar_t> : std::formatter<std::wstring, wchar_t> {
	template<class FormatContext>
	auto format(const std::filesystem::path& t, FormatContext& fc) const {
		return std::formatter<std::wstring, wchar_t>::format(t.c_str(), fc);
	}
};

template<>
struct std::formatter<std::filesystem::path, char> : std::formatter<std::string, char> {
	template<class FormatContext>
	auto format(const std::filesystem::path& t, FormatContext& fc) const {
		return std::formatter<std::string, char>::format(xivres::util::unicode::convert<std::string>(t.wstring()), fc);
	}
};

template<>
struct std::formatter<std::wstring, char> : std::formatter<std::string, char> {
	template<class FormatContext>
	auto format(const std::wstring& t, FormatContext& fc) const {
		return std::formatter<std::string, char>::format(xivres::util::unicode::convert<std::string>(t), fc);
	}
};

template<>
struct std::formatter<std::string, wchar_t> : std::formatter<std::wstring, wchar_t> {
	template<class FormatContext>
	auto format(const std::string& t, FormatContext& fc) const {
		return std::formatter<std::wstring, wchar_t>::format(xivres::util::unicode::convert<std::wstring>(t), fc);
	}
};

template<>
struct std::formatter<const wchar_t*, char> : std::formatter<std::string, char> {
	template<class FormatContext>
	auto format(const wchar_t* t, FormatContext& fc) const {
		return std::formatter<std::string, char>::format(xivres::util::unicode::convert<std::string>(t), fc);
	}
};

template<>
struct std::formatter<const char*, wchar_t> : std::formatter<std::wstring, wchar_t> {
	template<class FormatContext>
	auto format(const char* t, FormatContext& fc) const {
		return std::formatter<std::wstring, wchar_t>::format(xivres::util::unicode::convert<std::wstring>(t), fc);
	}
};
