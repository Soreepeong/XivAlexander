#pragma once

#include <string>
#include <vector>

namespace Utils {
	std::wstring FromUtf8(const std::string&);
	std::string ToUtf8(const std::wstring&);
	std::wstring FromOem(const std::string&);

	std::string ToString(const struct in_addr& ia);
	std::string ToString(const struct sockaddr_in& sa);
	std::string ToString(const struct sockaddr_in6& sa);
	std::string ToString(const struct sockaddr& sa);
	std::string ToString(const struct sockaddr_storage& sa);
	
	[[nodiscard]]
	std::vector<std::string> StringSplit(const std::string& str, const std::string& delimiter);

	[[nodiscard]]
	std::string StringTrim(const std::string& str, bool leftTrim = true, bool rightTrim = true);
}
