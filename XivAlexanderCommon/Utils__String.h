#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace Utils {
	std::wstring FromUtf8(const std::string&);
	std::string ToUtf8(const std::wstring&);
	
	std::string ToString(const struct sockaddr_in& sa);
	std::string ToString(const struct sockaddr_in6& sa);
	std::string ToString(const struct sockaddr& sa);
	std::string ToString(const struct sockaddr_storage& sa);

	[[nodiscard]]
	std::string FormatString(const _Printf_format_string_ char* format, ...);

	[[nodiscard]]
	std::wstring FormatString(const _Printf_format_string_ wchar_t* format, ...);

	[[nodiscard]]
	std::vector<std::string> StringSplit(const std::string& str, const std::string& delimiter);

	[[nodiscard]]
	std::string StringTrim(const std::string& str, bool leftTrim = true, bool rightTrim = true);
}
