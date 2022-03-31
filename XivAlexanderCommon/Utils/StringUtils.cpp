
#include "XivAlexanderCommon/Utils/StringUtils.h"
#include "XivAlexanderCommon/Utils/Win32.h"

std::wstring Utils::FromUtf8(std::string_view str, UINT codePage) {
	if (str.empty())
		return {};
	const size_t length = MultiByteToWideChar(codePage, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring wstr(length, 0);
	MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, str.data(), static_cast<int>(str.size()), const_cast<LPWSTR>(wstr.data()), static_cast<int>(wstr.size()));
	return wstr;
}

std::string Utils::ToUtf8(std::wstring_view wstr, UINT codePage) {
	if (wstr.empty())
		return {};
	const size_t length = WideCharToMultiByte(codePage, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string str(length, 0);
	WideCharToMultiByte(codePage, 0, wstr.data(), static_cast<int>(wstr.size()), const_cast<LPSTR>(str.c_str()), static_cast<int>(str.size()), nullptr, nullptr);
	return str;
}
