#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

#include <xivres/util.unicode.h>

namespace Utils {
	std::wstring FromAnsi(std::string_view str);
	std::string ToAnsi(std::wstring_view str);

	std::string ToString(const struct in_addr& ia);
	std::string ToString(const struct sockaddr_in& sa);
	std::string ToString(const struct sockaddr_in6& sa);
	std::string ToString(const struct sockaddr& sa);
	std::string ToString(const struct sockaddr_storage& sa);

	[[nodiscard]] inline std::string ToUtf8(const std::filesystem::path& path) {
		return xivres::util::unicode::convert<std::string>(path.wstring());
	}

	// Narrow strings fold ASCII letters only; wide strings compare as the file system does.
	[[nodiscard]] inline bool EqualsI(std::string_view a, std::string_view b) {
		const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
		return std::ranges::equal(a, b, [&lower](char x, char y) { return lower(x) == lower(y); });
	}

	[[nodiscard]] inline bool EqualsI(std::wstring_view a, std::wstring_view b) {
		return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
	}

	[[nodiscard]] inline bool StartsWithI(std::string_view s, std::string_view prefix) {
		return s.size() >= prefix.size() && EqualsI(s.substr(0, prefix.size()), prefix);
	}

	[[nodiscard]] inline bool StartsWithI(std::wstring_view s, std::wstring_view prefix) {
		return s.size() >= prefix.size() && EqualsI(s.substr(0, prefix.size()), prefix);
	}

	[[nodiscard]] inline bool EndsWithI(std::string_view s, std::string_view suffix) {
		return s.size() >= suffix.size() && EqualsI(s.substr(s.size() - suffix.size()), suffix);
	}

	[[nodiscard]] inline bool EndsWithI(std::wstring_view s, std::wstring_view suffix) {
		return s.size() >= suffix.size() && EqualsI(s.substr(s.size() - suffix.size()), suffix);
	}
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
