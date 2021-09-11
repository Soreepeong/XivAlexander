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

	template<typename T = std::string>
	[[nodiscard]] std::vector<T> StringSplit(const T& str, const T& delimiter) {
		std::vector<T> result;
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

	template<typename T = std::string>
	[[nodiscard]] T StringTrim(const T& str, bool leftTrim = true, bool rightTrim = true) {
		size_t left = 0, right = str.length() - 1;
		if (leftTrim)
			while (left < str.length() && std::isspace(static_cast<uint8_t>(str[left])))
				left++;
		if (rightTrim)
			while (right != SIZE_MAX && std::isspace(static_cast<uint8_t>(str[right])))
				--right;
		return str.substr(left, right + 1 - left);
	}

	template<typename T = std::string>
	[[nodiscard]] T StringReplaceAll(const T& source, const T& from, const T& to) {
		T s;
		s.reserve(source.length());

		size_t last = 0;
		size_t pos;

		while (T::npos != (pos = source.find(from, last))) {
			s.append(&source[last], &source[pos]);
			s += to;
			last = pos + from.length();
		}

		s += source.substr(last);
		return s;
	}

}
