#pragma once

#include <format>
#include <filesystem>
#include <string>

#include "XaStrings.h"

template<>
struct std::formatter<std::filesystem::path, wchar_t> : std::formatter<std::wstring, wchar_t> {
	template<class FormatContext>
	auto format(const std::filesystem::path& t, FormatContext& fc) {
		return std::formatter<std::wstring, wchar_t>::format(t.c_str(), fc);
	}
};

template<>
struct std::formatter<std::filesystem::path, char> : std::formatter<std::string, char> {
	template<class FormatContext>
	auto format(const std::filesystem::path& t, FormatContext& fc) {
		return std::formatter<std::string, char>::format(Utils::ToUtf8(t.wstring()), fc);
	}
};

template<>
struct std::formatter<std::wstring, char> : std::formatter<std::string, char> {
	template<class FormatContext>
	auto format(const std::wstring& t, FormatContext& fc) {
		return std::formatter<std::string, char>::format(Utils::ToUtf8(t), fc);
	}
};

template<>
struct std::formatter<std::string, wchar_t> : std::formatter<std::wstring, wchar_t> {
	template<class FormatContext>
	auto format(const std::string& t, FormatContext& fc) {
		return std::formatter<std::wstring, wchar_t>::format(Utils::FromUtf8(t), fc);
	}
};

template<>
struct std::formatter<const wchar_t*, char> : std::formatter<std::string, char> {
	template<class FormatContext>
	auto format(const wchar_t* t, FormatContext& fc) {
		return std::formatter<std::string, char>::format(Utils::ToUtf8(t), fc);
	}
};

template<>
struct std::formatter<const char*, wchar_t> : std::formatter<std::wstring, wchar_t> {
	template<class FormatContext>
	auto format(const char* t, FormatContext& fc) {
		return std::formatter<std::wstring, wchar_t>::format(Utils::FromUtf8(t), fc);
	}
};
